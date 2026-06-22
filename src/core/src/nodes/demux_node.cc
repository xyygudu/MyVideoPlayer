#include "nodes/demux_node.h"

#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

#include "graph/media_format.h"

namespace mvp::graph {

DemuxNode::DemuxNode(std::string file_path)
    : file_path_(std::move(file_path)) {}

DemuxNode::~DemuxNode() {
    Stop();
    CloseFormatContext();
}

bool DemuxNode::Negotiate() {
    // Source node: format is determined after Prepare opens the file.
    // Negotiate is a no-op for Source nodes; ports get their format in Prepare.
    return true;
}

bool DemuxNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;  // Already prepared
    }
    if (state_ != NodeState::kIdle && state_ != NodeState::kConfigured) {
        SPDLOG_ERROR("DemuxNode: Prepare called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    // Open file
    if (avformat_open_input(&format_ctx_, file_path_.c_str(), nullptr,
                            nullptr) < 0) {
        SPDLOG_ERROR("DemuxNode: failed to open '{}'", file_path_);
        state_ = NodeState::kError;
        return false;
    }

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        SPDLOG_ERROR("DemuxNode: failed to find stream info for '{}'",
                     file_path_);
        avformat_close_input(&format_ctx_);
        state_ = NodeState::kError;
        return false;
    }

    // Identify best audio/video streams
    audio_stream_index_ = -1;
    video_stream_index_ = -1;

    for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
        auto* codecpar = format_ctx_->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            video_stream_index_ < 0) {
            video_stream_index_ = static_cast<int>(i);
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                   audio_stream_index_ < 0) {
            audio_stream_index_ = static_cast<int>(i);
        }
    }

    if (audio_stream_index_ < 0 && video_stream_index_ < 0) {
        SPDLOG_ERROR("DemuxNode: no audio or video streams in '{}'",
                     file_path_);
        avformat_close_input(&format_ctx_);
        state_ = NodeState::kError;
        return false;
    }

    // Create output ports — one per selected stream.
    // We only create ports for audio and video streams we'll use.
    output_ports_.clear();

    if (video_stream_index_ >= 0) {
        auto port = std::make_unique<OutputPort>(this);
        auto* stream = format_ctx_->streams[video_stream_index_];
        Rational tb{stream->time_base.num, stream->time_base.den};
        Rational fr{stream->avg_frame_rate.num, stream->avg_frame_rate.den};
        port->SetFormat(MediaFormat::FromStream(
            stream->codecpar->codec_id, tb, fr,
            stream->codecpar, MediaType::kVideo));
        output_ports_.push_back(std::move(port));
    }

    if (audio_stream_index_ >= 0) {
        auto port = std::make_unique<OutputPort>(this);
        auto* stream = format_ctx_->streams[audio_stream_index_];
        Rational tb{stream->time_base.num, stream->time_base.den};
        Rational fr{0, 1};
        port->SetFormat(MediaFormat::FromStream(
            stream->codecpar->codec_id, tb, fr,
            stream->codecpar, MediaType::kAudio));
        output_ports_.push_back(std::move(port));
    }

    SPDLOG_INFO(
        "DemuxNode: opened '{}' — duration {:.2f}s, video={}, audio={}",
        file_path_, Duration(), video_stream_index_, audio_stream_index_);

    state_ = NodeState::kPrepared;
    return true;
}

bool DemuxNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("DemuxNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    running_ = true;
    seek_requested_ = false;
    demux_thread_ = std::thread(&DemuxNode::DemuxLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void DemuxNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }

    running_ = false;
    if (demux_thread_.joinable()) {
        demux_thread_.join();
    }
    state_ = NodeState::kIdle;
}

void DemuxNode::Flush() {
    // Signal seek to worker thread. The actual avformat_seek_file
    // is executed in DemuxLoop on next iteration.
    // Seek position must be set before calling Flush via RequestSeek().
    seek_requested_ = true;
}

void DemuxNode::RequestSeek(double position_seconds) {
    seek_position_.store(position_seconds, std::memory_order_release);
    seek_requested_.store(true, std::memory_order_release);
}

double DemuxNode::Duration() const {
    if (!format_ctx_ || format_ctx_->duration == AV_NOPTS_VALUE) {
        return 0.0;
    }
    return static_cast<double>(format_ctx_->duration) / AV_TIME_BASE;
}

std::vector<OutputPort*> DemuxNode::Outputs() {
    std::vector<OutputPort*> result;
    result.reserve(output_ports_.size());
    for (auto& port : output_ports_) {
        result.push_back(port.get());
    }
    return result;
}

void DemuxNode::CloseFormatContext() {
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    audio_stream_index_ = -1;
    video_stream_index_ = -1;
    output_ports_.clear();
}

void DemuxNode::DemuxLoop() {
    // Port index mapping: video port is always [0] if exists, audio follows.
    OutputPort* video_port = nullptr;
    OutputPort* audio_port = nullptr;
    int port_idx = 0;
    if (video_stream_index_ >= 0) {
        video_port = output_ports_[port_idx++].get();
    }
    if (audio_stream_index_ >= 0) {
        audio_port = output_ports_[port_idx++].get();
    }

    while (running_.load(std::memory_order_relaxed)) {
        // Handle seek
        if (seek_requested_.load(std::memory_order_acquire)) {
            double pos = seek_position_.load(std::memory_order_acquire);
            int64_t timestamp = static_cast<int64_t>(pos * AV_TIME_BASE);
            av_seek_frame(format_ctx_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
            seek_requested_.store(false, std::memory_order_release);
            SPDLOG_DEBUG("DemuxNode: seek to {:.2f}s", pos);
        }

        AVPacketPtr pkt;
        int ret = av_read_frame(format_ctx_, pkt.get());

        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(format_ctx_->pb)) {
                // Send EOS to each connected output port.
                if (video_port && video_port->IsConnected()) {
                    video_port->Push(
                        MediaBuffer::MakeEos(MediaType::kVideo));
                }
                if (audio_port && audio_port->IsConnected()) {
                    audio_port->Push(
                        MediaBuffer::MakeEos(MediaType::kAudio));
                }
            } else {
                SPDLOG_WARN("DemuxNode: av_read_frame error {}", ret);
            }

            // Wait for seek or stop.
            while (running_.load(std::memory_order_relaxed) &&
                   !seek_requested_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        // Route packet to appropriate port.
        int stream_index = pkt->stream_index;
        OutputPort* target = nullptr;
        MediaType type = MediaType::kUnknown;

        if (stream_index == video_stream_index_ && video_port) {
            target = video_port;
            type = MediaType::kVideo;
        } else if (stream_index == audio_stream_index_ && audio_port) {
            target = audio_port;
            type = MediaType::kAudio;
        }

        if (target && target->IsConnected()) {
            auto* stream = format_ctx_->streams[stream_index];
            double pts_sec = (pkt->pts != AV_NOPTS_VALUE)
                                 ? pkt->pts * av_q2d(stream->time_base)
                                 : 0.0;

            Timestamp ts;
            ts.pts = pts_sec;
            ts.dts = (pkt->dts != AV_NOPTS_VALUE)
                         ? pkt->dts * av_q2d(stream->time_base)
                         : pts_sec;
            ts.duration = pkt->duration * av_q2d(stream->time_base);
            ts.time_base = {stream->time_base.num, stream->time_base.den};

            BufferFlags flags = BufferFlags::kNone;
            if (pkt->flags & AV_PKT_FLAG_KEY) {
                flags = flags | BufferFlags::kKeyFrame;
            }

            MediaBuffer buf(std::move(pkt), type, ts, flags);
            target->Push(std::move(buf));
        }
    }
}

}  // namespace mvp::graph
