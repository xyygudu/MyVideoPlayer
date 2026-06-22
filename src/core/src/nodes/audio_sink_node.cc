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
#include "graph/media_format.h"
#include "media_frame.h"

namespace mvp::graph {

AudioSinkNode::AudioSinkNode() {
    input_port_ = std::make_unique<InputPort>(this);
}

AudioSinkNode::~AudioSinkNode() {
    Stop();
    CloseDevice();
}

bool AudioSinkNode::Negotiate() {
    return true;
}

void AudioSinkNode::SetAudioClock(mvp::Clock* clock) {
    audio_clock_ = clock;
}

bool AudioSinkNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    if (state_ != NodeState::kIdle && state_ != NodeState::kConfigured) {
        SPDLOG_ERROR("AudioSinkNode: Prepare in invalid state");
        return false;
    }

    // Get audio parameters from input port format.
    // Decoder output is a frame format (sample_rate/channels fields),
    // NOT a packet format (codec_params). Use MediaFormat accessors directly.
    const auto& fmt = input_port_->Format();
    if (fmt.sample_rate() > 0 && fmt.channels() > 0) {
        sample_rate_ = fmt.sample_rate();
        channels_ = fmt.channels();
    } else if (fmt.codec_params()) {
        // Fallback: raw codec params (e.g. direct DemuxNode→Sink without decoder)
        sample_rate_ = fmt.codec_params()->sample_rate;
        channels_ = fmt.codec_params()->ch_layout.nb_channels;
    } else {
        SPDLOG_ERROR("AudioSinkNode: no audio params from port format "
                     "(sample_rate={}, channels={}, codec_params={})",
                     fmt.sample_rate(), fmt.channels(),
                     fmt.codec_params() ? "valid" : "null");
        state_ = NodeState::kError;
        return false;
    }

    // Initialize SDL audio
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SPDLOG_ERROR("AudioSinkNode: SDL_InitSubSystem failed: {}",
                     SDL_GetError());
        state_ = NodeState::kError;
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
        state_ = NodeState::kError;
        return false;
    }

    SPDLOG_INFO("AudioSinkNode: opened (rate={}, channels={})",
                sample_rate_, channels_);
    state_ = NodeState::kPrepared;
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

void AudioSinkNode::AudioLoop() {
    SwrContext* swr_ctx = nullptr;

    while (running_.load(std::memory_order_relaxed)) {
        if (paused_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Back-pressure: don't overwhelm SDL buffer.
        // Keep ~100ms of audio buffered.
        int queued = SDL_GetAudioStreamQueued(sdl_stream_);
        int target_bytes = sample_rate_ * channels_ * 2 / 10;  // 100ms of S16
        if (queued > target_bytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Pull next buffer from input link
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }

        MediaBuffer& buf = *opt_buf;

        // EOS → report and exit
        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            // Wait until SDL finishes playing buffered audio
            while (running_.load(std::memory_order_relaxed) &&
                   SDL_GetAudioStreamQueued(sdl_stream_) > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (graph_) {
                graph_->ReportEvent(GraphEvent::kEos);
            }
            break;
        }

        if (!buf.IsFrame()) {
            continue;
        }

        MediaFrame& mf = buf.AsFrame();
        if (!mf.IsValid()) {
            continue;
        }

        // Update audio clock (this is the MasterClock source in AudioMaster mode)
        if (audio_clock_) {
            audio_clock_->Set(mf.pts());
        }

        AVFrame* frame = mf.RawFrame();

        // Convert to S16 and feed SDL
        int out_samples = frame->nb_samples;
        int out_buffer_size = out_samples * channels_ * 2;  // S16 = 2 bytes/sample

        if (frame->format != AV_SAMPLE_FMT_S16) {
            // Lazy-init resampler
            if (!swr_ctx) {
                AVChannelLayout out_layout;
                av_channel_layout_default(&out_layout, channels_);
                swr_alloc_set_opts2(&swr_ctx, &out_layout, AV_SAMPLE_FMT_S16,
                                    sample_rate_, &frame->ch_layout,
                                    static_cast<AVSampleFormat>(frame->format),
                                    frame->sample_rate, 0, nullptr);
                swr_init(swr_ctx);
                av_channel_layout_uninit(&out_layout);
            }

            uint8_t* out_buf = static_cast<uint8_t*>(av_malloc(out_buffer_size));
            uint8_t* out_planes[] = {out_buf};
            swr_convert(swr_ctx, out_planes, out_samples,
                        const_cast<const uint8_t**>(frame->data),
                        frame->nb_samples);
            SDL_PutAudioStreamData(sdl_stream_, out_buf, out_buffer_size);
            av_free(out_buf);
        } else {
            SDL_PutAudioStreamData(sdl_stream_, frame->data[0],
                                   out_buffer_size);
        }
    }

    if (swr_ctx) {
        swr_free(&swr_ctx);
    }
}

}  // namespace mvp::graph
