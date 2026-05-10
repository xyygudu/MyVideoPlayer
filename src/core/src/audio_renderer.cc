#include "audio_renderer.h"

#include "clock.h"
#include "frame_impl.h"
#include "frame_queue.h"
#include "mvp/audio_frame.h"
#include "packet_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

#include <SDL3/SDL.h>

namespace mvp {

AudioRenderer::AudioRenderer()
    : frame_queue_(nullptr),
      packet_queue_(nullptr),
      audio_clock_(nullptr),
      sdl_stream_(nullptr),
      running_(false),
      paused_(false) {}

AudioRenderer::~AudioRenderer() { Close(); }

bool AudioRenderer::Open(AVStream* stream) {
    Close();

    // Cache stream parameters as value types
    sample_rate_ = stream->codecpar->sample_rate;
    channels_ = stream->codecpar->ch_layout.nb_channels;

    // Initialize SDL audio subsystem
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SPDLOG_ERROR("AudioRenderer: SDL_InitSubSystem failed: {}", SDL_GetError());
        return false;
    }

    // Set up audio spec based on codec parameters
    SDL_AudioSpec src_spec;
    src_spec.freq = sample_rate_;
    src_spec.channels = channels_;
    src_spec.format = SDL_AUDIO_S16;

    sdl_stream_ =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src_spec, nullptr, nullptr);
    if (!sdl_stream_) {
        SPDLOG_ERROR("AudioRenderer: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        return false;
    }

    SPDLOG_INFO("AudioRenderer: opened (rate={}, channels={})", sample_rate_, channels_);
    return true;
}

void AudioRenderer::Close() {
    Stop();

    if (sdl_stream_) {
        SDL_DestroyAudioStream(sdl_stream_);
        sdl_stream_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioRenderer::Start(FrameQueue<AudioFrame>* frame_queue, PacketQueue* packet_queue,
                           Clock* audio_clock) {
    if (running_) return;
    frame_queue_ = frame_queue;
    packet_queue_ = packet_queue;
    audio_clock_ = audio_clock;

    running_ = true;
    paused_ = false;
    SDL_ResumeAudioStreamDevice(sdl_stream_);
    audio_thread_ = std::thread(&AudioRenderer::AudioLoop, this);
}

void AudioRenderer::Stop() {
    running_ = false;
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
}

void AudioRenderer::SetPaused(bool paused) {
    paused_ = paused;
    if (sdl_stream_) {
        if (paused) {
            SDL_PauseAudioStreamDevice(sdl_stream_);
        } else {
            SDL_ResumeAudioStreamDevice(sdl_stream_);
        }
    }
}

void AudioRenderer::FlushSdlBuffer() {
    if (sdl_stream_) {
        SDL_ClearAudioStream(sdl_stream_);
    }
}

void AudioRenderer::SetEofCallback(EofCallback cb) { eof_cb_ = std::move(cb); }

void AudioRenderer::AudioLoop() {
    SwrContext* swr_ctx = nullptr;

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Check if SDL needs more data
        int queued = SDL_GetAudioStreamQueued(sdl_stream_);
        // Keep ~100ms of audio buffered in SDL
        int target_bytes = sample_rate_ * channels_ * 2 / 10;
        if (queued > target_bytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto entry = frame_queue_->Pop();
        if (!entry) {
            break;  // Aborted
        }

        // EOF marker received — notify player and exit loop
        if (entry->eof) {
            if (eof_cb_) eof_cb_();
            break;
        }

        int frame_serial = entry->serial;

        // Discard stale frames from before seek
        int current_serial = packet_queue_->serial();
        if (frame_serial != current_serial) {
            continue;
        }

        // Update audio clock with pre-computed PTS (already in seconds)
        AudioFrame& af = entry->frame;
        audio_clock_->Set(af.pts());

        AVFrame* frame = GetInternalFrame(af);

        // Convert to S16 format if needed
        int out_samples = frame->nb_samples;
        int out_buffer_size = out_samples * channels_ * 2;  // S16 = 2 bytes

        if (frame->format != AV_SAMPLE_FMT_S16) {
            if (!swr_ctx) {
                AVChannelLayout out_layout;
                av_channel_layout_default(&out_layout, channels_);
                swr_alloc_set_opts2(&swr_ctx, &out_layout, AV_SAMPLE_FMT_S16,
                                    sample_rate_, &frame->ch_layout,
                                    static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                                    0, nullptr);
                swr_init(swr_ctx);
                av_channel_layout_uninit(&out_layout);
            }

            uint8_t* out_buf = static_cast<uint8_t*>(av_malloc(out_buffer_size));
            uint8_t* out_planes[] = {out_buf};
            swr_convert(swr_ctx, out_planes, out_samples, const_cast<const uint8_t**>(frame->data),
                        frame->nb_samples);
            SDL_PutAudioStreamData(sdl_stream_, out_buf, out_buffer_size);
            av_free(out_buf);
        } else {
            SDL_PutAudioStreamData(sdl_stream_, frame->data[0], out_buffer_size);
        }
    }

    if (swr_ctx) swr_free(&swr_ctx);
}

}  // namespace mvp
