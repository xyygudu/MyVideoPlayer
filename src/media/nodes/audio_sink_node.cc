#include "nodes/audio_sink_node.h"

#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "audio_resampler.h"
#include "clock.h"
#include "graph/graph_command.h"
#include "graph/media_format.h"
#include "media_frame.h"

namespace mvp::graph {
namespace {
// Audio is the most stable time base: SDL drains it at a fixed device rate,
// so it outranks any other offer in the playback graph.
constexpr int kClockPriority = 100;

// How much audio to keep queued in SDL to absorb feed jitter. Compensated out
// of the clock, so changing it does not shift A/V sync.
constexpr double kQueueTargetSeconds = 0.1;
}  // namespace

AudioSinkNode::AudioSinkNode() {
    input_port_ = std::make_unique<InputPort>(this);
}

AudioSinkNode::~AudioSinkNode() {
    Stop();
    CloseDevice();
}

bool AudioSinkNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("AudioSinkNode: input port not connected");
        return false;
    }
    return ReadAudioParams();
}

ClockOffer AudioSinkNode::ProvideClock() {
    return {clock_, kClockPriority};
}

bool AudioSinkNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    if (state_ != NodeState::kIdle && state_ != NodeState::kConfigured) {
        SPDLOG_ERROR("AudioSinkNode: Prepare in invalid state");
        return false;
    }

    if (!OpenSdlDevice()) {
        state_ = NodeState::kError;
        return false;
    }
    // The SDL stream format is fixed here; every frame is converted to it.
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, channels_);
    resampler_ = std::make_unique<AudioResampler>(AV_SAMPLE_FMT_S16, sample_rate_, layout);
    av_channel_layout_uninit(&layout);

    SPDLOG_INFO("AudioSinkNode: opened (rate={}, channels={})", sample_rate_,
                channels_);
    state_ = NodeState::kPrepared;
    return true;
}

bool AudioSinkNode::ReadAudioParams() {
    // Audio parameters come from the input port's decoded AudioFormat.
    const auto& fmt = input_port_->Format();
    if (fmt.IsAudio()) {
        sample_rate_ = fmt.AsAudio().sample_rate;
        channels_ = fmt.AsAudio().channels;
        return true;
    }
    // Fallback: direct DemuxNode->Sink without decoder (rare).
    if (fmt.IsEncoded() && fmt.AsEncoded().codec_params) {
        const auto* cp = fmt.AsEncoded().codec_params.get();
        sample_rate_ = cp->sample_rate;
        channels_ = cp->ch_layout.nb_channels;
        return true;
    }
    SPDLOG_ERROR("AudioSinkNode: no audio params from input port format");
    return false;
}

bool AudioSinkNode::OpenSdlDevice() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SPDLOG_ERROR("AudioSinkNode: SDL_InitSubSystem failed: {}",
                     SDL_GetError());
        return false;
    }

    SDL_AudioSpec src_spec;
    src_spec.freq = sample_rate_;
    src_spec.channels = channels_;
    src_spec.format = SDL_AUDIO_S16;

    sdl_stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src_spec, nullptr, nullptr);
    if (!sdl_stream_) {
        SPDLOG_ERROR("AudioSinkNode: SDL_OpenAudioDeviceStream failed: {}",
                     SDL_GetError());
        return false;
    }
    return true;
}

bool AudioSinkNode::Start() {
    if (state_ != NodeState::kPrepared) {
        return false;
    }
    running_ = true;
    paused_ = false;
    SDL_ResumeAudioStreamDevice(sdl_stream_);
    audio_thread_ = std::thread(&AudioSinkNode::AudioLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void AudioSinkNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }
    running_ = false;
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
    state_ = NodeState::kIdle;
}

void AudioSinkNode::Flush() {
    // The graph bumped the epoch first, so after this clear the audio thread
    // rejects whatever pre-seek frame it still holds (see FeedFrame).
    FlushSdlBuffer();
}

void AudioSinkNode::SetPaused(bool paused) {
    paused_ = paused;
    if (sdl_stream_) {
        if (paused) {
            SDL_PauseAudioStreamDevice(sdl_stream_);
        } else {
            SDL_ResumeAudioStreamDevice(sdl_stream_);
        }
    }
}

void AudioSinkNode::FlushSdlBuffer() {
    std::lock_guard lock(feed_mutex_);
    if (sdl_stream_) {
        SDL_ClearAudioStream(sdl_stream_);
    }
}

void AudioSinkNode::OnCommand(const Command& /*cmd*/) {
    // Seek: the SDL queue was already cleared by Flush(), which the graph
    // always runs first; nothing else to reset here.
}

std::vector<InputPort*> AudioSinkNode::Inputs() {
    return {input_port_.get()};
}

void AudioSinkNode::CloseDevice() {
    if (sdl_stream_) {
        SDL_DestroyAudioStream(sdl_stream_);
        sdl_stream_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioSinkNode::ShouldThrottle() const {
    if (paused_.load(std::memory_order_relaxed)) {
        return true;
    }
    return QueuedSeconds() > kQueueTargetSeconds;
}

double AudioSinkNode::QueuedSeconds() const {
    const double bytes_per_sec =
        static_cast<double>(sample_rate_) * channels_ * 2;  // S16
    if (!sdl_stream_ || bytes_per_sec <= 0.0) {
        return 0.0;
    }
    return SDL_GetAudioStreamQueued(sdl_stream_) / bytes_per_sec;
}

bool AudioSinkNode::IsCurrentEpoch(int serial) const {
    return !graph_ || serial == graph_->SeekEpoch();
}

void AudioSinkNode::FeedFrame(const MediaBuffer& buf) {
    if (!resampler_->Convert(buf.AsFrame().RawFrame(), converted_.get()) ||
        converted_->nb_samples == 0) {
        return;  // Failures are logged by the resampler; 0 = still buffering.
    }
    const int bytes = av_samples_get_buffer_size(nullptr, channels_, converted_->nb_samples,
                                                 AV_SAMPLE_FMT_S16, 1);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard lock(feed_mutex_);
    if (!IsCurrentEpoch(buf.serial())) {
        return;  // A seek happened while this frame was being converted.
    }
    // Measured before feeding: the queue then spans exactly [heard, pts),
    // so the difference is the position the user is hearing right now.
    clock_->Set(buf.timestamp().pts - QueuedSeconds(), buf.serial());
    if (!SDL_PutAudioStreamData(sdl_stream_, converted_->data[0], bytes)) {
        SPDLOG_WARN("AudioSinkNode: SDL_PutAudioStreamData failed: {}", SDL_GetError());
    }
}

void AudioSinkNode::DrainAndReportEos(int serial) {
    // Report only once SDL played everything out. A seek meanwhile clears the
    // queue and makes this EOS stale, so it is dropped instead.
    while (running_.load(std::memory_order_relaxed) && IsCurrentEpoch(serial) &&
           SDL_GetAudioStreamQueued(sdl_stream_) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (graph_ && running_.load(std::memory_order_relaxed) && IsCurrentEpoch(serial)) {
        graph_->Bus().Post({GraphMessageType::kEos, this, serial, {}});
    }
}

void AudioSinkNode::AudioLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        if (ShouldThrottle()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        const MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            DrainAndReportEos(buf.serial());
            continue;
        }
        if (buf.IsFrame() && buf.AsFrame().IsValid()) {
            FeedFrame(buf);
        }
    }
}

}  // namespace mvp::graph
