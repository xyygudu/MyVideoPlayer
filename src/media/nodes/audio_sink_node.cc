#include "nodes/audio_sink_node.h"

#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

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
    if (sdl_stream_) {
        SDL_ClearAudioStream(sdl_stream_);
    }
}

void AudioSinkNode::OnCommand(const Command& cmd) {
    if (cmd.type == CommandType::kSeek) {
        FlushSdlBuffer();
    }
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

void AudioSinkNode::ConvertAndFeed(AVFrame* frame) {
    int out_samples = frame->nb_samples;
    int out_buffer_size = out_samples * channels_ * 2;  // S16 = 2 bytes/sample

    if (frame->format == AV_SAMPLE_FMT_S16) {
        SDL_PutAudioStreamData(sdl_stream_, frame->data[0], out_buffer_size);
        return;
    }

    // Lazy-init resampler (owned by this audio thread)
    if (!swr_ctx_) {
        AVChannelLayout out_layout, in_layout;
        av_channel_layout_default(&out_layout, channels_);

        // 检查输入布局有效性，无效则降级
        if (av_channel_layout_check(&frame->ch_layout)) {
            av_channel_layout_copy(&in_layout, &frame->ch_layout);
        } else {
            av_channel_layout_default(&in_layout, frame->ch_layout.nb_channels);
        }

        int ret = swr_alloc_set_opts2(&swr_ctx_,
                                    &out_layout, AV_SAMPLE_FMT_S16, sample_rate_,
                                    &in_layout,
                                    static_cast<AVSampleFormat>(frame->format),
                                    frame->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);

        if (ret < 0 || !swr_ctx_) {
            SPDLOG_ERROR("AudioSinkNode: ConvertAndFeed failed, swr_ctx_ is nullptr");
            swr_ctx_ = nullptr; // 确保不会野指针
            return;
        }
        
        ret = swr_init(swr_ctx_);
        if (ret < 0) {
            SPDLOG_ERROR("AudioSinkNode: ConvertAndFeed failed, swr_init returned error");
            swr_free(&swr_ctx_);
            return;
        }
    }

    uint8_t* out_buf = static_cast<uint8_t*>(av_malloc(out_buffer_size));
    uint8_t* out_planes[] = {out_buf};
    swr_convert(swr_ctx_, out_planes, out_samples,
                const_cast<const uint8_t**>(frame->data), frame->nb_samples);
    SDL_PutAudioStreamData(sdl_stream_, out_buf, out_buffer_size);
    av_free(out_buf);
}

void AudioSinkNode::DrainAndReportEos() {
    // Wait until SDL finishes playing buffered audio, then report EOS.
    while (running_.load(std::memory_order_relaxed) &&
           SDL_GetAudioStreamQueued(sdl_stream_) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (graph_) {
        graph_->ReportEvent(GraphEvent::kEos);
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
        MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            DrainAndReportEos();
            continue;
        }
        if (!buf.IsFrame()) {
            continue;
        }
        MediaFrame& mf = buf.AsFrame();
        if (!mf.IsValid()) {
            continue;
        }

        // Measured before feeding: the queue then spans exactly [heard, pts),
        // so the difference is the position the user is hearing right now.
        clock_->Set(buf.timestamp().pts - QueuedSeconds());
        ConvertAndFeed(mf.RawFrame());
    }

    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
}

}  // namespace mvp::graph
