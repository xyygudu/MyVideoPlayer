#include "mvp/player.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "audio_renderer.h"
#include "clock.h"
#include "demuxer.h"
#include "mvp/player_state.h"
#include "stream_context.h"
#include "sync_constants.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace mvp {

class PlayerImpl {
  public:
    PlayerImpl();
    ~PlayerImpl();

    bool Open(const std::string& filepath);
    void Close();
    void Play();
    void Pause();
    void Seek(double position_seconds);
    void StepFrame();
    double Duration() const;
    double CurrentPosition() const;
    double CurrentVideoPosition() const;
    double VideoFps() const;
    bool IsPlaying() const;
    PlayerState State() const;
    void SetVideoFrameCallback(Player::VideoFrameCallback cb);
    void SetPlaybackFinishedCallback(std::function<void()> cb);

  private:
    void VideoRenderLoop();
    void WaitIfPaused();
    Clock& MasterClock();
    const Clock& MasterClock() const;
    bool TransitionTo(PlayerState target);
    void OnStreamEof();

    Demuxer demuxer_;
    std::unique_ptr<StreamContext> audio_ctx_;
    std::unique_ptr<StreamContext> video_ctx_;
    std::unique_ptr<AudioRenderer> audio_renderer_;

    Clock audio_clock_;
    Clock video_clock_;
    SyncMode sync_mode_{SyncMode::AudioMaster};
    std::atomic<double> video_pts_{0.0};
    double video_fps_{0.0};

    std::atomic<PlayerState> state_{PlayerState::Idle};
    std::atomic<double> seek_target_{sync::kNoSeekTarget};

    // Frame stepping (Paused state only)
    std::mutex step_mutex_;
    std::condition_variable step_cond_;
    bool frame_step_requested_{false};

    // EOF tracking
    std::atomic<bool> audio_eof_{false};
    std::atomic<bool> video_eof_{false};

    Player::VideoFrameCallback video_frame_cb_;
    std::function<void()> playback_finished_cb_;

    std::thread video_render_thread_;
};

// ---------------------------------------------------------------------------
// State transition table
// ---------------------------------------------------------------------------

bool PlayerImpl::TransitionTo(PlayerState target) {
    PlayerState current = state_.load(std::memory_order_acquire);
    while (true) {
        // Validate transition
        bool valid = false;
        switch (target) {
            case PlayerState::Idle: valid = true; break;  // Any → Idle (Close)
            case PlayerState::Ready:
                valid = (current == PlayerState::Idle);
                break;
            case PlayerState::Playing:
                valid = (current == PlayerState::Ready || current == PlayerState::Paused ||
                         current == PlayerState::Finished);
                break;
            case PlayerState::Paused:
                valid = (current == PlayerState::Playing || current == PlayerState::Finished);
                break;
            case PlayerState::Finished:
                valid = (current == PlayerState::Playing || current == PlayerState::Paused);
                break;
        }
        if (!valid) {
            SPDLOG_WARN("Player: invalid state transition {} → {}",
                        static_cast<int>(current), static_cast<int>(target));
            return false;
        }
        if (state_.compare_exchange_weak(current, target, std::memory_order_acq_rel)) {
            return true;
        }
        // current was updated by compare_exchange_weak, retry
    }
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

PlayerImpl::PlayerImpl() {}

PlayerImpl::~PlayerImpl() { Close(); }

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

bool PlayerImpl::Open(const std::string& filepath) {
    Close();

    if (!demuxer_.Open(filepath)) {
        SPDLOG_ERROR("Player: failed to open '{}'", filepath);
        return false;
    }

    // Determine sync mode
    if (demuxer_.AudioStreamIndex() >= 0) {
        sync_mode_ = SyncMode::AudioMaster;
    } else {
        sync_mode_ = SyncMode::VideoMaster;
    }

    // Create audio stream context
    if (demuxer_.AudioStreamIndex() >= 0) {
        audio_ctx_ = std::make_unique<StreamContext>(sync::kDefaultAudioQueueSize);
        AVStream* audio_stream = demuxer_.FormatContext()->streams[demuxer_.AudioStreamIndex()];
        if (!audio_ctx_->OpenDecoder(audio_stream)) {
            audio_ctx_.reset();
        } else {
            // Create audio renderer (SDL output)
            audio_renderer_ = std::make_unique<AudioRenderer>();
            if (!audio_renderer_->Open(audio_stream)) {
                audio_renderer_.reset();
            } else {
                audio_renderer_->SetEofCallback([this] { OnStreamEof(); });
            }
        }
    }

    // Create video stream context
    if (demuxer_.VideoStreamIndex() >= 0) {
        video_ctx_ = std::make_unique<StreamContext>(sync::kDefaultVideoQueueSize);
        AVStream* video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
        if (!video_ctx_->OpenDecoder(video_stream)) {
            video_ctx_.reset();
        }
    }

    // Cache video FPS
    if (demuxer_.VideoStreamIndex() >= 0) {
        AVStream* vs = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
        if (vs->avg_frame_rate.den > 0) {
            video_fps_ = av_q2d(vs->avg_frame_rate);
        }
    }

    TransitionTo(PlayerState::Ready);
    SPDLOG_INFO("Player: opened '{}' (sync={})", filepath,
                sync_mode_ == SyncMode::AudioMaster ? "AudioMaster" : "VideoMaster");
    return true;
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

void PlayerImpl::Close() {
    if (state_.load() == PlayerState::Idle) return;
    SPDLOG_INFO("Player: closing");

    // Signal all to stop
    if (audio_ctx_) audio_ctx_->Abort();
    if (video_ctx_) video_ctx_->Abort();

    // Wake up render thread if paused
    {
        std::lock_guard<std::mutex> lock(step_mutex_);
        frame_step_requested_ = true;
    }
    step_cond_.notify_one();

    demuxer_.Stop();
    if (audio_renderer_) audio_renderer_->Stop();

    if (video_render_thread_.joinable()) {
        video_render_thread_.join();
    }

    audio_renderer_.reset();
    audio_ctx_.reset();
    video_ctx_.reset();
    demuxer_.Close();

    audio_clock_.Reset();
    video_clock_.Reset();
    video_pts_.store(0.0, std::memory_order_relaxed);
    seek_target_.store(sync::kNoSeekTarget, std::memory_order_relaxed);
    audio_eof_.store(false, std::memory_order_relaxed);
    video_eof_.store(false, std::memory_order_relaxed);

    state_.store(PlayerState::Idle, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------

void PlayerImpl::Play() {
    PlayerState s = state_.load();
    if (s == PlayerState::Playing) return;

    if (s == PlayerState::Finished) {
        // Restart from beginning
        Seek(0.0);
    }

    if (s == PlayerState::Ready || s == PlayerState::Finished) {
        TransitionTo(PlayerState::Playing);
        SPDLOG_INFO("Player: play (starting threads)");

        audio_eof_.store(false, std::memory_order_relaxed);
        video_eof_.store(false, std::memory_order_relaxed);

        // Start demux
        PacketQueue* audio_q = audio_ctx_ ? &audio_ctx_->packet_queue : nullptr;
        PacketQueue* video_q = video_ctx_ ? &video_ctx_->packet_queue : nullptr;
        demuxer_.Start(audio_q, video_q);

        // Start audio decode + render
        if (audio_ctx_) {
            audio_ctx_->Start(false);
            if (audio_renderer_) {
                audio_renderer_->Start(&audio_ctx_->frame_queue,
                                       &audio_ctx_->packet_queue, &audio_clock_);
            }
        }

        // Start video decode
        if (video_ctx_) {
            video_ctx_->Start(true);
        }

        // Start video render loop
        video_render_thread_ = std::thread(&PlayerImpl::VideoRenderLoop, this);
    } else if (s == PlayerState::Paused) {
        // Resume from pause
        TransitionTo(PlayerState::Playing);
        SPDLOG_INFO("Player: resumed from pause");
        audio_clock_.SetPaused(false);
        video_clock_.SetPaused(false);
        if (audio_renderer_) audio_renderer_->SetPaused(false);

        // Wake render thread
        step_cond_.notify_one();
    }
}

// ---------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------

void PlayerImpl::Pause() {
    if (state_.load() != PlayerState::Playing) return;
    TransitionTo(PlayerState::Paused);
    SPDLOG_INFO("Player: paused");

    audio_clock_.SetPaused(true);
    video_clock_.SetPaused(true);
    if (audio_renderer_) audio_renderer_->SetPaused(true);
}

// ---------------------------------------------------------------------------
// Seek
// ---------------------------------------------------------------------------

void PlayerImpl::Seek(double position_seconds) {
    PlayerState s = state_.load();
    if (s == PlayerState::Idle) return;

    SPDLOG_INFO("Player: seek to {:.2f}s", position_seconds);

    // If finished, transition to paused to allow seeking
    if (s == PlayerState::Finished) {
        TransitionTo(PlayerState::Paused);
    }

    // Flush all stream contexts
    if (audio_ctx_) audio_ctx_->Flush();
    if (video_ctx_) video_ctx_->Flush();
    if (audio_renderer_) audio_renderer_->FlushSdlBuffer();

    // Set seek target for frame-accurate discard in VideoRenderLoop
    seek_target_.store(position_seconds, std::memory_order_release);

    // Reset EOF state
    audio_eof_.store(false, std::memory_order_relaxed);
    video_eof_.store(false, std::memory_order_relaxed);

    demuxer_.RequestSeek(position_seconds);

    // Reset master clock to target position
    audio_clock_.Set(position_seconds);
    video_clock_.Set(position_seconds);

    // If paused, request one frame to show the seek target
    if (state_.load() == PlayerState::Paused) {
        std::lock_guard<std::mutex> lock(step_mutex_);
        frame_step_requested_ = true;
        step_cond_.notify_one();
    }
}

// ---------------------------------------------------------------------------
// StepFrame
// ---------------------------------------------------------------------------

void PlayerImpl::StepFrame() {
    if (state_.load() != PlayerState::Paused) return;
    std::lock_guard<std::mutex> lock(step_mutex_);
    frame_step_requested_ = true;
    step_cond_.notify_one();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

double PlayerImpl::Duration() const { return demuxer_.Duration(); }

double PlayerImpl::CurrentPosition() const { return MasterClock().Get(); }

double PlayerImpl::CurrentVideoPosition() const {
    return video_pts_.load(std::memory_order_relaxed);
}

double PlayerImpl::VideoFps() const { return video_fps_; }

bool PlayerImpl::IsPlaying() const { return state_.load() == PlayerState::Playing; }

PlayerState PlayerImpl::State() const { return state_.load(std::memory_order_acquire); }

void PlayerImpl::SetVideoFrameCallback(Player::VideoFrameCallback cb) {
    video_frame_cb_ = std::move(cb);
}

void PlayerImpl::SetPlaybackFinishedCallback(std::function<void()> cb) {
    playback_finished_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// MasterClock
// ---------------------------------------------------------------------------

Clock& PlayerImpl::MasterClock() {
    return (sync_mode_ == SyncMode::AudioMaster) ? audio_clock_ : video_clock_;
}

const Clock& PlayerImpl::MasterClock() const {
    return (sync_mode_ == SyncMode::AudioMaster) ? audio_clock_ : video_clock_;
}

// ---------------------------------------------------------------------------
// EOF handling
// ---------------------------------------------------------------------------

void PlayerImpl::OnStreamEof() {
    // Called from audio renderer thread when it receives EOF marker.
    // For video, it's detected in VideoRenderLoop.
    audio_eof_.store(true, std::memory_order_release);

    bool all_done = true;
    if (audio_ctx_ && !audio_eof_.load()) all_done = false;
    if (video_ctx_ && !video_eof_.load()) all_done = false;

    if (all_done) {
        if (TransitionTo(PlayerState::Finished)) {
            SPDLOG_INFO("Player: playback finished (EOF)");
            if (playback_finished_cb_) playback_finished_cb_();
        }
    }
}

// ---------------------------------------------------------------------------
// WaitIfPaused — blocks render thread until resumed or step requested
// ---------------------------------------------------------------------------

void PlayerImpl::WaitIfPaused() {
    std::unique_lock<std::mutex> lock(step_mutex_);
    step_cond_.wait(lock, [this] {
        return state_.load() != PlayerState::Paused || frame_step_requested_;
    });
}

// ---------------------------------------------------------------------------
// VideoRenderLoop
// ---------------------------------------------------------------------------

void PlayerImpl::VideoRenderLoop() {
    AVFrame* frame = av_frame_alloc();
    AVStream* video_stream = nullptr;
    if (demuxer_.VideoStreamIndex() >= 0) {
        video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
    }

    double last_pts = 0.0;
    double last_display_time = Clock::Now();

    while (state_.load() != PlayerState::Idle) {
        // Wait if paused (uses condition variable, not polling)
        if (state_.load() == PlayerState::Paused && !frame_step_requested_) {
            WaitIfPaused();
            // After waking: check if we should exit or step
            if (state_.load() == PlayerState::Idle) break;
        }

        bool stepping = false;
        {
            std::lock_guard<std::mutex> lock(step_mutex_);
            stepping = frame_step_requested_;
        }

        // Pop frame from video frame queue
        int frame_serial = 0;
        bool is_eof = false;
        if (!video_ctx_->frame_queue.Pop(frame, &frame_serial, &is_eof)) {
            break;  // Aborted
        }

        // EOF marker → notify and exit
        if (is_eof) {
            video_eof_.store(true, std::memory_order_release);
            // Check if all streams are done
            bool all_done = true;
            if (audio_ctx_ && !audio_eof_.load()) all_done = false;
            if (all_done) {
                if (TransitionTo(PlayerState::Finished)) {
                    SPDLOG_INFO("Player: playback finished (EOF)");
                    if (playback_finished_cb_) playback_finished_cb_();
                }
            }
            break;
        }

        // Discard stale frames (serial mismatch)
        int current_serial = video_ctx_->packet_queue.serial();
        if (frame_serial != current_serial) {
            av_frame_unref(frame);
            continue;
        }

        // Calculate PTS
        double pts = 0.0;
        if (frame->pts != AV_NOPTS_VALUE && video_stream) {
            pts = static_cast<double>(frame->pts) * av_q2d(video_stream->time_base);
        }

        // Frame-accurate seek: discard frames before seek target
        double target = seek_target_.load(std::memory_order_acquire);
        if (target > sync::kNoSeekTarget && pts < target - 0.001) {
            av_frame_unref(frame);
            continue;
        }
        // Reached target — clear seek state
        if (target > sync::kNoSeekTarget) {
            seek_target_.store(sync::kNoSeekTarget, std::memory_order_release);
        }

        // --- Synchronization ---
        if (!stepping) {
            if (sync_mode_ == SyncMode::AudioMaster) {
                // Sync video to audio clock
                double master_time = audio_clock_.Get();
                double diff = pts - master_time;

                if (diff > sync::kSyncThreshold) {
                    // Video is ahead — wait (capped to avoid long stalls)
                    double wait = std::min(diff, sync::kMaxSleepSeconds);
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(static_cast<int64_t>(wait * 1e6)));
                } else if (diff < -sync::kDropThreshold) {
                    // Video is too late — drop frame
                    av_frame_unref(frame);
                    continue;
                }
            } else {
                // VideoMaster: self-driven by frame interval + wall-time
                double delay = pts - last_pts;
                if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
                    delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
                }

                double target_time = last_display_time + delay;
                double now = Clock::Now();
                double wait = target_time - now;
                if (wait > sync::kFrameDelayMin) {
                    wait = std::min(wait, sync::kMaxSleepSeconds);
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(static_cast<int64_t>(wait * 1e6)));
                }
                last_display_time = Clock::Now();
                video_clock_.Set(pts);
            }
        }

        // Clear step flag after rendering a valid frame
        if (stepping) {
            std::lock_guard<std::mutex> lock(step_mutex_);
            frame_step_requested_ = false;
        }

        // Deliver frame via callback
        if (video_frame_cb_ && frame->data[0]) {
            video_frame_cb_(frame->data[0], frame->width, frame->height, frame->linesize[0]);
        }

        video_pts_.store(pts, std::memory_order_relaxed);
        last_pts = pts;
        av_frame_unref(frame);
    }

    av_frame_free(&frame);
}

// ===========================================================================
// Player public interface delegation
// ===========================================================================

Player::Player() : impl_(std::make_unique<PlayerImpl>()) {}
Player::~Player() = default;

bool Player::Open(const std::string& filepath) { return impl_->Open(filepath); }
void Player::Close() { impl_->Close(); }
void Player::Play() { impl_->Play(); }
void Player::Pause() { impl_->Pause(); }
void Player::Seek(double position_seconds) { impl_->Seek(position_seconds); }
void Player::StepFrame() { impl_->StepFrame(); }
double Player::Duration() const { return impl_->Duration(); }
double Player::CurrentPosition() const { return impl_->CurrentPosition(); }
double Player::CurrentVideoPosition() const { return impl_->CurrentVideoPosition(); }
double Player::VideoFps() const { return impl_->VideoFps(); }
bool Player::IsPlaying() const { return impl_->IsPlaying(); }
PlayerState Player::State() const { return impl_->State(); }
void Player::SetVideoFrameCallback(VideoFrameCallback cb) {
    impl_->SetVideoFrameCallback(std::move(cb));
}
void Player::SetPlaybackFinishedCallback(std::function<void()> cb) {
    impl_->SetPlaybackFinishedCallback(std::move(cb));
}

}  // namespace mvp
