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

AVStream* Demuxer::AudioStream() const {
    if (!format_ctx_ || audio_stream_index_ < 0) return nullptr;
    return format_ctx_->streams[audio_stream_index_];
}

AVStream* Demuxer::VideoStream() const {
    if (!format_ctx_ || video_stream_index_ < 0) return nullptr;
    return format_ctx_->streams[video_stream_index_];
}

void Demuxer::DemuxLoop() {
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

        AVPacketPtr pkt;
        int ret = av_read_frame(format_ctx_, pkt.get());
        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(format_ctx_->pb)) {
                // Push null (flush) packets to signal EOF to decoders.
                // A null packet triggers drain mode in avcodec_send_packet.
                if (audio_queue_) {
                    AVPacketPtr eof_pkt;
                    audio_queue_->Push(SerialPacket{std::move(eof_pkt), audio_serial});
                }
                if (video_queue_) {
                    AVPacketPtr eof_pkt;
                    video_queue_->Push(SerialPacket{std::move(eof_pkt), video_serial});
                }
            }
            // Wait for a seek request or stop signal
            while (running_ && !seek_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        if (pkt.get()->stream_index == audio_stream_index_ && audio_queue_) {
            audio_queue_->Push(SerialPacket{std::move(pkt), audio_serial});
        } else if (pkt.get()->stream_index == video_stream_index_ && video_queue_) {
            video_queue_->Push(SerialPacket{std::move(pkt), video_serial});
        }
    }
}

}  // namespace mvp
