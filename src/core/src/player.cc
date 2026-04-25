#include "mvp/player.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "audio_output.h"
#include "clock.h"
#include "decoder.h"
#include "demuxer.h"
#include "frame_queue.h"
#include "packet_queue.h"

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
    double Duration() const;
    double CurrentPosition() const;
    bool IsPlaying() const;
    void SetVideoFrameCallback(Player::VideoFrameCallback cb);

  private:
    void VideoRenderLoop();

    Demuxer demuxer_;
    std::unique_ptr<Decoder> video_decoder_;
    std::unique_ptr<AudioOutput> audio_output_;

    PacketQueue audio_packet_queue_;
    PacketQueue video_packet_queue_;
    FrameQueue video_frame_queue_{3};

    Clock audio_clock_;

    Player::VideoFrameCallback video_frame_cb_;

    std::thread video_render_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
};

PlayerImpl::PlayerImpl() : running_(false), paused_(false) {}

PlayerImpl::~PlayerImpl() { Close(); }

bool PlayerImpl::Open(const std::string& filepath) {
    Close();

    if (!demuxer_.Open(filepath)) {
        SPDLOG_ERROR("Player: failed to open '{}'", filepath);
        return false;
    }

    // Open audio
    if (demuxer_.AudioStreamIndex() >= 0) {
        audio_output_ = std::make_unique<AudioOutput>();
        AVStream* audio_stream = demuxer_.FormatContext()->streams[demuxer_.AudioStreamIndex()];
        if (!audio_output_->Open(audio_stream, &audio_packet_queue_, &audio_clock_)) {
            audio_output_.reset();
        }
    }

    // Open video decoder
    if (demuxer_.VideoStreamIndex() >= 0) {
        video_decoder_ = std::make_unique<Decoder>();
        AVStream* video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
        if (!video_decoder_->Open(video_stream)) {
            video_decoder_.reset();
        }
    }

    SPDLOG_INFO("Player: opened '{}'", filepath);
    return true;
}

void PlayerImpl::Close() {
    SPDLOG_INFO("Player: closing");
    running_ = false;
    paused_ = false;

    // Abort all queues to unblock threads
    audio_packet_queue_.Abort();
    video_packet_queue_.Abort();
    video_frame_queue_.Abort();

    demuxer_.Stop();
    if (audio_output_) audio_output_->Stop();
    if (video_decoder_) video_decoder_->Stop();

    if (video_render_thread_.joinable()) {
        video_render_thread_.join();
    }

    audio_output_.reset();
    video_decoder_.reset();
    demuxer_.Close();

    // Flush queues
    audio_packet_queue_.Flush();
    video_packet_queue_.Flush();
    video_frame_queue_.Flush();
    audio_clock_.Reset();
}

void PlayerImpl::Play() {
    if (running_ && !paused_) return;

    if (!running_) {
        running_ = true;
        paused_ = false;
        SPDLOG_INFO("Player: play (starting threads)");

        // Start demux
        demuxer_.Start(&audio_packet_queue_, &video_packet_queue_);

        // Start audio
        if (audio_output_) {
            audio_output_->Start();
        }

        // Start video decode
        if (video_decoder_) {
            AVStream* video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
            video_decoder_->Start(&video_packet_queue_, &video_frame_queue_, true);
        }

        // Start video render loop
        video_render_thread_ = std::thread(&PlayerImpl::VideoRenderLoop, this);
    } else {
        // Resume from pause
        SPDLOG_INFO("Player: resumed from pause");
        paused_ = false;
        if (audio_output_) audio_output_->SetPaused(false);
    }
}

void PlayerImpl::Pause() {
    if (!running_ || paused_) return;
    SPDLOG_INFO("Player: paused");
    paused_ = true;
    if (audio_output_) audio_output_->SetPaused(true);
}

void PlayerImpl::Seek(double position_seconds) {
    SPDLOG_INFO("Player: seek to {:.2f}s", position_seconds);
    // Flush queues
    audio_packet_queue_.Flush();
    video_packet_queue_.Flush();
    video_frame_queue_.Flush();

    // Flush decoders
    if (audio_output_ && audio_output_->GetDecoder()) {
        audio_output_->GetDecoder()->FlushBuffers();
    }
    if (video_decoder_) {
        video_decoder_->FlushBuffers();
    }

    // Request demuxer to seek
    demuxer_.RequestSeek(position_seconds);
    audio_clock_.Set(position_seconds);
}

double PlayerImpl::Duration() const { return demuxer_.Duration(); }

double PlayerImpl::CurrentPosition() const { return audio_clock_.Get(); }

bool PlayerImpl::IsPlaying() const { return running_ && !paused_; }

void PlayerImpl::SetVideoFrameCallback(Player::VideoFrameCallback cb) {
    video_frame_cb_ = std::move(cb);
}

void PlayerImpl::VideoRenderLoop() {
    AVFrame* frame = av_frame_alloc();
    AVStream* video_stream = nullptr;
    if (demuxer_.VideoStreamIndex() >= 0) {
        video_stream = demuxer_.FormatContext()->streams[demuxer_.VideoStreamIndex()];
    }

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!video_frame_queue_.Pop(frame)) {
            break;  // Aborted
        }

        // Calculate PTS
        double pts = 0.0;
        if (frame->pts != AV_NOPTS_VALUE && video_stream) {
            pts = static_cast<double>(frame->pts) * av_q2d(video_stream->time_base);
        }

        // Sync to audio clock
        double audio_time = audio_clock_.Get();
        double diff = pts - audio_time;

        if (diff > 0.01) {
            // Video is ahead, wait
            int wait_ms = static_cast<int>(diff * 1000);
            if (wait_ms > 100) wait_ms = 100;  // Cap wait
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        } else if (diff < -0.1) {
            // Video is behind, drop frame
            av_frame_unref(frame);
            continue;
        }

        // Deliver frame via callback
        if (video_frame_cb_ && frame->data[0]) {
            video_frame_cb_(frame->data[0], frame->width, frame->height);
        }

        av_frame_unref(frame);
    }

    av_frame_free(&frame);
}

// Player public interface delegation
Player::Player() : impl_(std::make_unique<PlayerImpl>()) {}
Player::~Player() = default;

bool Player::Open(const std::string& filepath) { return impl_->Open(filepath); }
void Player::Close() { impl_->Close(); }
void Player::Play() { impl_->Play(); }
void Player::Pause() { impl_->Pause(); }
void Player::Seek(double position_seconds) { impl_->Seek(position_seconds); }
double Player::Duration() const { return impl_->Duration(); }
double Player::CurrentPosition() const { return impl_->CurrentPosition(); }
bool Player::IsPlaying() const { return impl_->IsPlaying(); }
void Player::SetVideoFrameCallback(VideoFrameCallback cb) {
    impl_->SetVideoFrameCallback(std::move(cb));
}

}  // namespace mvp
