#include "mvp/player.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "audio_renderer.h"
#include "clock.h"
#include "demuxer.h"
#include "frame_converter.h"
#include "mvp/player_state.h"
#include "stream_context.h"
#include "sync_constants.h"
#include "video_renderer.h"

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
    void SetWindowHandle(void* native_handle);
    void NotifyWindowResized(int width, int height);

  private:
    void VideoRenderLoop();
    void WaitIfPaused();
    Clock& MasterClock();
    const Clock& MasterClock() const;
    bool TransitionTo(PlayerState target);
    void OnStreamEof();

    // Pipeline lifecycle
    void StopPipeline();
    void ResetPipeline();
    void StartPipeline(bool audio_paused);

    // Video render helpers
    double ComputeDisplayDelay(double pts, double last_pts,
                               double last_display_time);
    void CheckAllStreamsEof();

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

    // frame_timer: absolute wall-clock anchor for cumulative sync correction
    double frame_timer_{0.0};

    // EOF tracking
    std::atomic<bool> audio_eof_{false};
    std::atomic<bool> video_eof_{false};

    Player::VideoFrameCallback video_frame_cb_;
    std::function<void()> playback_finished_cb_;

    void* window_handle_{nullptr};
    int window_width_{0};
    int window_height_{0};
    VideoRenderer video_renderer_;

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
            default:
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

PlayerImpl::PlayerImpl() = default;

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

    StopPipeline();

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
        StopPipeline();
        ResetPipeline();
        audio_clock_.Set(0.0);
        video_clock_.Set(0.0);
        audio_clock_.SetPaused(false);
        video_clock_.SetPaused(false);
        seek_target_.store(sync::kNoSeekTarget, std::memory_order_relaxed);
        demuxer_.RequestSeek(0.0);
        s = PlayerState::Ready;
    }

    if (s == PlayerState::Ready) {
        TransitionTo(PlayerState::Playing);
        SPDLOG_INFO("Player: play (starting threads)");
        audio_eof_.store(false, std::memory_order_relaxed);
        video_eof_.store(false, std::memory_order_relaxed);
        StartPipeline(false);
    } else if (s == PlayerState::Paused) {
        TransitionTo(PlayerState::Playing);
        SPDLOG_INFO("Player: resumed from pause");
        audio_clock_.SetPaused(false);
        video_clock_.SetPaused(false);
        if (audio_renderer_) audio_renderer_->SetPaused(false);
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

    if (s == PlayerState::Finished) {
        StopPipeline();
        ResetPipeline();
        audio_clock_.Set(position_seconds);
        video_clock_.Set(position_seconds);
        audio_clock_.SetPaused(true);
        video_clock_.SetPaused(true);
        seek_target_.store(position_seconds, std::memory_order_release);
        TransitionTo(PlayerState::Paused);
        demuxer_.RequestSeek(position_seconds);
        StartPipeline(true);
        {
            std::lock_guard<std::mutex> lock(step_mutex_);
            frame_step_requested_ = true;
        }
        step_cond_.notify_one();
        return;
    }

    // Flush all stream contexts
    if (audio_ctx_) audio_ctx_->Flush();
    if (video_ctx_) video_ctx_->Flush();
    if (audio_renderer_) audio_renderer_->FlushSdlBuffer();

    seek_target_.store(position_seconds, std::memory_order_release);
    audio_eof_.store(false, std::memory_order_relaxed);
    video_eof_.store(false, std::memory_order_relaxed);
    demuxer_.RequestSeek(position_seconds);
    audio_clock_.Set(position_seconds);
    video_clock_.Set(position_seconds);

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

void PlayerImpl::SetWindowHandle(void* native_handle) {
    window_handle_ = native_handle;
}

void PlayerImpl::NotifyWindowResized(int width, int height) {
    window_width_ = width;
    window_height_ = height;
    video_renderer_.Resize(width, height);
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
    audio_eof_.store(true, std::memory_order_release);
    CheckAllStreamsEof();
}

// ---------------------------------------------------------------------------
// CheckAllStreamsEof — transitions to Finished if all streams have ended
// ---------------------------------------------------------------------------

void PlayerImpl::CheckAllStreamsEof() {
    bool all_done = true;
    if (audio_ctx_ && !audio_eof_.load()) all_done = false;
    if (video_ctx_ && !video_eof_.load()) all_done = false;

    if (all_done) {
        if (TransitionTo(PlayerState::Finished)) {
            SPDLOG_INFO("Player: playback finished (EOF)");
            audio_clock_.SetPaused(true);
            video_clock_.SetPaused(true);
            if (playback_finished_cb_) playback_finished_cb_();
        }
    }
}

// ---------------------------------------------------------------------------
// Pipeline lifecycle
// ---------------------------------------------------------------------------

void PlayerImpl::StopPipeline() {
    if (audio_ctx_) audio_ctx_->Abort();
    if (video_ctx_) video_ctx_->Abort();

    // Wake render thread if blocked in WaitIfPaused
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

    video_renderer_.Close();
}

void PlayerImpl::ResetPipeline() {
    if (audio_ctx_) {
        audio_ctx_->packet_queue.Reset();
        audio_ctx_->frame_queue.Reset();
    }
    if (video_ctx_) {
        video_ctx_->packet_queue.Reset();
        video_ctx_->frame_queue.Reset();
    }
    if (audio_renderer_) audio_renderer_->FlushSdlBuffer();
    audio_eof_.store(false, std::memory_order_relaxed);
    video_eof_.store(false, std::memory_order_relaxed);
}

void PlayerImpl::StartPipeline(bool audio_paused) {
    PacketQueue* audio_q = audio_ctx_ ? &audio_ctx_->packet_queue : nullptr;
    PacketQueue* video_q = video_ctx_ ? &video_ctx_->packet_queue : nullptr;
    demuxer_.Start(audio_q, video_q);

    if (audio_ctx_) {
        audio_ctx_->Start();
        if (audio_renderer_) {
            audio_renderer_->Start(&audio_ctx_->frame_queue,
                                   &audio_ctx_->packet_queue, &audio_clock_);
            if (audio_paused) audio_renderer_->SetPaused(true);
        }
    }
    if (video_ctx_) {
        video_ctx_->Start();

        // Open SDL video renderer if window handle was provided
        if (window_handle_ && !video_renderer_.IsOpen()) {
            int w = window_width_ > 0 ? window_width_ : 640;
            int h = window_height_ > 0 ? window_height_ : 480;
            video_renderer_.Open(window_handle_, w, h);
        }
    }

    video_render_thread_ = std::thread(&PlayerImpl::VideoRenderLoop, this);
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
// ComputeDisplayDelay — timing calculation for A/V sync
// Returns: seconds to wait (>=0). No longer returns negative (frame_timer
// reset replaces the old drop-frame logic in AudioMaster mode).
// ---------------------------------------------------------------------------

double PlayerImpl::ComputeDisplayDelay(double pts, double last_pts,
                                       double last_display_time) {
    if (sync_mode_ == SyncMode::AudioMaster) {
        // 1. Frame interval
        double delay = pts - last_pts;
        if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
            delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
        }

        // 2. Audio diff
        double diff = pts - audio_clock_.Get();

        // 3. Adaptive sync threshold (clamped to perceptible range)
        double sync_threshold = std::clamp(delay, sync::kSyncThresholdMin,
                                           sync::kSyncThresholdMax);

        // 4. Correct delay based on diff
        if (diff > sync_threshold) {
            // Video ahead: low framerate (long interval) corrects in one step,
            // high framerate spreads correction across multiple frames.
            if (delay > sync::kSyncThresholdMax) {
                delay += diff;
            } else {
                delay = 2 * delay;
            }
        } else if (diff < -sync_threshold) {
            delay = 0.0;    // Video behind → display immediately
        }

        // 5. Accumulate to absolute timeline
        frame_timer_ += delay;

        // 6. Compute actual wait from wall-clock
        double now = Clock::Now();
        double actual_wait = frame_timer_ - now;

        // 7. Reset on large discontinuity (seek/pause recovery)
        if (actual_wait < -sync::kMaxSleepSeconds) {
            frame_timer_ = now;
            return 0.0;
        }

        if (actual_wait > 0.0) {
            return std::min(actual_wait, sync::kMaxSleepSeconds);
        }
        return 0.0;
    }

    // VideoMaster: self-driven by frame interval + wall-time
    double delay = pts - last_pts;
    if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }
    double target_time = last_display_time + delay;
    double now = Clock::Now();
    double wait = target_time - now;
    if (wait > sync::kFrameDelayMin) {
        return std::min(wait, sync::kMaxSleepSeconds);
    }
    return 0.0;
}

// ---------------------------------------------------------------------------
// VideoRenderLoop
// ---------------------------------------------------------------------------

void PlayerImpl::VideoRenderLoop() {
    AVStream* video_stream = nullptr;
    if (demuxer_.VideoStreamIndex() >= 0) {
        video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
    }

    double last_pts = 0.0;
    double last_display_time = Clock::Now();
    frame_timer_ = Clock::Now();

    while (state_.load() != PlayerState::Idle) {
        if (state_.load() == PlayerState::Paused && !frame_step_requested_) {
            WaitIfPaused();
            if (state_.load() == PlayerState::Idle) break;
        }

        bool stepping = false;
        {
            std::lock_guard<std::mutex> lock(step_mutex_);
            stepping = frame_step_requested_;
        }

        auto sf = video_ctx_->frame_queue.Pop();
        if (!sf) {
            break;
        }

        if (sf->eof) {
            video_eof_.store(true, std::memory_order_release);
            CheckAllStreamsEof();
            break;
        }

        int frame_serial = sf->serial;
        AVFrame* frame = sf->frame.get();

        // Discard stale frames (serial mismatch)
        if (frame_serial != video_ctx_->packet_queue.serial()) {
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
            continue;
        }
        if (target > sync::kNoSeekTarget) {
            seek_target_.store(sync::kNoSeekTarget, std::memory_order_release);
        }

        // A/V synchronization
        if (!stepping) {
            double delay = ComputeDisplayDelay(pts, last_pts, last_display_time);
            if (delay > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<int64_t>(delay * 1e6)));
            }
            if (sync_mode_ == SyncMode::VideoMaster) {
                last_display_time = Clock::Now();
                video_clock_.Set(pts);
            }
        }

        if (stepping) {
            std::lock_guard<std::mutex> lock(step_mutex_);
            frame_step_requested_ = false;
        }

        if (video_frame_cb_ && frame->data[0]) {
            VideoFrame vf = FrameConverter::ToVideoFrame(frame, video_stream);
            video_renderer_.Render(vf);
            video_frame_cb_(vf);
        } else if (frame->data[0]) {
            VideoFrame vf = FrameConverter::ToVideoFrame(frame, video_stream);
            video_renderer_.Render(vf);
        }

        video_pts_.store(pts, std::memory_order_relaxed);
        last_pts = pts;
        // sf goes out of scope, AVFramePtr handles cleanup
    }
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
void Player::SetWindowHandle(void* native_handle) {
    impl_->SetWindowHandle(native_handle);
}
void Player::NotifyWindowResized(int width, int height) {
    impl_->NotifyWindowResized(width, height);
}

}  // namespace mvp
