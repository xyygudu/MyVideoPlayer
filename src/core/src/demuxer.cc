#include "demuxer.h"

#include "packet_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

namespace mvp {

Demuxer::Demuxer()
    : format_ctx_(nullptr),
      audio_stream_index_(-1),
      video_stream_index_(-1),
      audio_queue_(nullptr),
      video_queue_(nullptr),
      running_(false),
      seek_requested_(false),
      seek_position_(0.0) {}

Demuxer::~Demuxer() { Close(); }

bool Demuxer::Open(const std::string& filepath) {
    Close();

    if (avformat_open_input(&format_ctx_, filepath.c_str(), nullptr, nullptr) < 0) {
        SPDLOG_ERROR("Demuxer: failed to open '{}'", filepath);
        return false;
    }

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        SPDLOG_ERROR("Demuxer: failed to find stream info for '{}'", filepath);
        avformat_close_input(&format_ctx_);
        return false;
    }

    audio_stream_index_ = -1;
    video_stream_index_ = -1;

    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            audio_stream_index_ < 0) {
            audio_stream_index_ = static_cast<int>(i);
        }
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            video_stream_index_ < 0) {
            video_stream_index_ = static_cast<int>(i);
        }
    }

    if (audio_stream_index_ >= 0 || video_stream_index_ >= 0) {
        SPDLOG_INFO("Demuxer: opened '{}' — {} streams, duration {:.2f}s, audio={}, video={}",
                    filepath, format_ctx_->nb_streams, Duration(), audio_stream_index_,
                    video_stream_index_);
    }
    return audio_stream_index_ >= 0 || video_stream_index_ >= 0;
}

void Demuxer::Close() {
    Stop();
    if (format_ctx_) {
        SPDLOG_INFO("Demuxer: closing");
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    audio_stream_index_ = -1;
    video_stream_index_ = -1;
}

void Demuxer::Start(PacketQueue* audio_queue, PacketQueue* video_queue) {
    if (running_) return;
    audio_queue_ = audio_queue;
    video_queue_ = video_queue;
    running_ = true;
    demux_thread_ = std::thread(&Demuxer::DemuxLoop, this);
}

void Demuxer::Stop() {
    running_ = false;
    if (demux_thread_.joinable()) {
        demux_thread_.join();
    }
}

void Demuxer::RequestSeek(double position_seconds) {
    seek_position_ = position_seconds;
    seek_requested_ = true;
}

double Demuxer::Duration() const {
    if (!format_ctx_ || format_ctx_->duration == AV_NOPTS_VALUE) {
        return 0.0;
    }
    return static_cast<double>(format_ctx_->duration) / AV_TIME_BASE;
}

void Demuxer::DemuxLoop() {
    AVPacket* pkt = av_packet_alloc();
    // Local serial copies: only updated after seek, so pre-seek packets keep old serial
    int audio_serial = audio_queue_ ? audio_queue_->serial() : 0;
    int video_serial = video_queue_ ? video_queue_->serial() : 0;

    while (running_) {
        // Handle seek request
        if (seek_requested_) {
            double pos = seek_position_;
            int64_t timestamp = static_cast<int64_t>(pos * AV_TIME_BASE);
            av_seek_frame(format_ctx_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            seek_requested_ = false;
            // Read latest serial AFTER seek (main thread already flushed+incremented)
            if (audio_queue_) audio_serial = audio_queue_->serial();
            if (video_queue_) video_serial = video_queue_->serial();
        }

        int ret = av_read_frame(format_ctx_, pkt);
        if (ret < 0) {
            // EOF or error — wait for a seek request instead of exiting
            while (running_ && !seek_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (pkt->stream_index == audio_stream_index_ && audio_queue_) {
            audio_queue_->Push(pkt, audio_serial);
        } else if (pkt->stream_index == video_stream_index_ && video_queue_) {
            video_queue_->Push(pkt, video_serial);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

}  // namespace mvp
