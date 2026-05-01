#include "audio_output.h"

#include <memory>

#include "clock.h"
#include "decoder.h"
#include "frame_queue.h"
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

AudioOutput::AudioOutput()
    : packet_queue_(nullptr),
      audio_clock_(nullptr),
      stream_(nullptr),
      sdl_stream_(nullptr),
      running_(false),
      paused_(false) {}

AudioOutput::~AudioOutput() { Close(); }

bool AudioOutput::Open(AVStream* stream, PacketQueue* packet_queue, Clock* audio_clock) {
    Close();
    stream_ = stream;
    packet_queue_ = packet_queue;
    audio_clock_ = audio_clock;

    // Initialize SDL audio subsystem
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SPDLOG_ERROR("AudioOutput: SDL_InitSubSystem failed: {}", SDL_GetError());
        return false;
    }

    // Set up audio spec based on codec parameters
    SDL_AudioSpec src_spec;
    src_spec.freq = stream->codecpar->sample_rate;
    src_spec.channels = stream->codecpar->ch_layout.nb_channels;
    src_spec.format = SDL_AUDIO_S16;

    sdl_stream_ =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src_spec, nullptr, nullptr);
    if (!sdl_stream_) {
        SPDLOG_ERROR("AudioOutput: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
        return false;
    }

    // Create decoder and frame queue for audio
    decoder_ = std::make_unique<Decoder>();
    audio_frame_queue_ = std::make_unique<FrameQueue>(9);

    if (!decoder_->Open(stream)) {
        SPDLOG_ERROR("AudioOutput: failed to open audio decoder");
        return false;
    }

    SPDLOG_INFO("AudioOutput: opened (rate={}, channels={})", stream->codecpar->sample_rate,
                stream->codecpar->ch_layout.nb_channels);
    return true;
}

void AudioOutput::Close() {
    Stop();
    decoder_.reset();
    audio_frame_queue_.reset();

    if (sdl_stream_) {
        SDL_DestroyAudioStream(sdl_stream_);
        sdl_stream_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioOutput::Start() {
    if (running_) return;

    // Start audio decoder thread
    decoder_->Start(packet_queue_, audio_frame_queue_.get(), false);

    running_ = true;
    paused_ = false;
    SDL_ResumeAudioStreamDevice(sdl_stream_);
    audio_thread_ = std::thread(&AudioOutput::AudioLoop, this);
}

void AudioOutput::Stop() {
    running_ = false;
    if (audio_frame_queue_) audio_frame_queue_->Abort();
    if (decoder_) decoder_->Stop();
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
}

void AudioOutput::SetPaused(bool paused) {
    paused_ = paused;
    if (sdl_stream_) {
        if (paused) {
            SDL_PauseAudioStreamDevice(sdl_stream_);
        } else {
            SDL_ResumeAudioStreamDevice(sdl_stream_);
        }
    }
}

void AudioOutput::FlushFrameQueue() {
    if (audio_frame_queue_) {
        audio_frame_queue_->FlushAndIncrementSerial();
    }
    if (sdl_stream_) {
        SDL_ClearAudioStream(sdl_stream_);
    }
}

void AudioOutput::AudioLoop() {
    AVFrame* frame = av_frame_alloc();
    SwrContext* swr_ctx = nullptr;
    int frame_serial = 0;

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Check if SDL needs more data
        int queued = SDL_GetAudioStreamQueued(sdl_stream_);
        // Keep ~100ms of audio buffered in SDL
        int target_bytes =
            stream_->codecpar->sample_rate * stream_->codecpar->ch_layout.nb_channels * 2 / 10;
        if (queued > target_bytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!audio_frame_queue_->Pop(frame, &frame_serial)) {
            break;  // Aborted
        }

        // Discard stale frames from before seek (compare against packet queue serial)
        int current_serial = packet_queue_->serial();
        if (frame_serial != current_serial) {
            av_frame_unref(frame);
            continue;
        }

        // Update audio clock
        if (frame->pts != AV_NOPTS_VALUE) {
            double pts = static_cast<double>(frame->pts) * av_q2d(stream_->time_base);
            audio_clock_->Set(pts);
        }

        // Convert to S16 format if needed
        int out_samples = frame->nb_samples;
        int out_channels = stream_->codecpar->ch_layout.nb_channels;
        int out_buffer_size = out_samples * out_channels * 2;  // S16 = 2 bytes

        if (frame->format != AV_SAMPLE_FMT_S16) {
            if (!swr_ctx) {
                AVChannelLayout out_layout;
                av_channel_layout_default(&out_layout, out_channels);
                swr_alloc_set_opts2(&swr_ctx, &out_layout, AV_SAMPLE_FMT_S16,
                                    stream_->codecpar->sample_rate, &frame->ch_layout,
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

        av_frame_unref(frame);
    }

    av_frame_free(&frame);
    if (swr_ctx) swr_free(&swr_ctx);
}

}  // namespace mvp
