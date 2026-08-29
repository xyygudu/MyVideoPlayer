#include "nodes/demux_node.h"

#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

#include "graph/graph_command.h"
#include "graph/media_format.h"
#include "graph/media_graph.h"
#include "demux_node.h"

namespace mvp::graph {

DemuxNode::DemuxNode(std::string file_path, int video_stream_idx,
                     int audio_stream_idx)
    : file_path_(std::move(file_path)),
      video_stream_index_(video_stream_idx),
      audio_stream_index_(audio_stream_idx) {
    // 暂定output_ports_的顺序为video port在前，audio port在后
    if (video_stream_index_ >= 0) {
        auto video_port = std::make_unique<OutputPort>(this);
        output_ports_.push_back(std::move(video_port));
    }
    if (audio_stream_index_ >= 0) {
        auto audio_port = std::make_unique<OutputPort>(this);
        output_ports_.push_back(std::move(audio_port));
    }
}

DemuxNode::~DemuxNode() {
    Stop();
    CloseFormatContext();
}

bool DemuxNode::Open() {
    if (!OpenFile()) {
        return false;
    }
    if (audio_stream_index_ < 0 && video_stream_index_ < 0) {
        SPDLOG_ERROR("DemuxNode: no audio or video streams in '{}'", file_path_);
        return false;
    }
    state_ = NodeState::kOpened;
    SPDLOG_INFO("DemuxNode: opened '{}' - duration {:.2f}s, video={}, audio={}",
                file_path_, Duration(), video_stream_index_,
                audio_stream_index_);
    return true;
}

bool DemuxNode::Negotiate() {
    if (!format_ctx_) {
        SPDLOG_ERROR("DemuxNode: Negotiate before Open");
        return false;
    }
    if (video_stream_index_ >= 0) {
        if (video_stream_index_ >= static_cast<int>(format_ctx_->nb_streams)) {
            SPDLOG_ERROR("DemuxNode: video index {} out of range (nb_streams={})",
                         video_stream_index_, format_ctx_->nb_streams);
            return false;
        }
        auto* s = format_ctx_->streams[video_stream_index_];
        output_ports_[0]->SetFormat(MakeStreamFormat(video_stream_index_, MediaType::kVideo, {s->avg_frame_rate.num, s->avg_frame_rate.den}));
    }
    if (audio_stream_index_ >= 0) {
        if (audio_stream_index_ >= static_cast<int>(format_ctx_->nb_streams)) {
            SPDLOG_ERROR("DemuxNode: audio index {} out of range (nb_streams={})",
                         audio_stream_index_, format_ctx_->nb_streams);
            return false;
        }
        output_ports_[1]->SetFormat(MakeStreamFormat(audio_stream_index_, MediaType::kAudio, {0, 1}));
    }

    return true;
}

bool DemuxNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;  // Already prepared
    }
    if (state_ != NodeState::kOpened) {
        SPDLOG_ERROR("DemuxNode: Prepare called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    state_ = NodeState::kPrepared;
    return true;
}

bool DemuxNode::OpenFile() {
    if (format_ctx_) {
        return true;  // Already open (Prepare may be called multiple times)
    }
    if (avformat_open_input(&format_ctx_, file_path_.c_str(), nullptr,
                            nullptr) < 0) {
        SPDLOG_ERROR("DemuxNode: failed to open '{}'", file_path_);
        return false;
    }
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        SPDLOG_ERROR("DemuxNode: failed to find stream info for '{}'",
                     file_path_);
        avformat_close_input(&format_ctx_);
        return false;
    }
    return true;
}

void DemuxNode::FindStreams() {
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
}

MediaFormat DemuxNode::MakeStreamFormat(int stream_index, MediaType type,
                                        Rational frame_rate) const {
    auto* stream = format_ctx_->streams[stream_index];
    Rational tb{stream->time_base.num, stream->time_base.den};
    return MediaFormat::FromStream(stream->codecpar->codec_id, tb, frame_rate,
                                   stream->codecpar, type);
}

bool DemuxNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("DemuxNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    running_ = true;
    pending_seek_.store(kNoSeekPending, std::memory_order_release);
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
    // Reposition happens in OnCommand(kSeek) -> RequestSeek() instead.
}

void DemuxNode::RequestSeek(double position_seconds) {
    pending_seek_.store(position_seconds, std::memory_order_release);
}

void DemuxNode::OnCommand(const Command& cmd) {
    if (cmd.type == CommandType::kSeek) {
        RequestSeek(cmd.position);
    }
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

bool DemuxNode::HandlePendingSeek() {
    double pos = pending_seek_.exchange(kNoSeekPending, std::memory_order_acq_rel);
    if (pos == kNoSeekPending) {
        return false;
    }
    int64_t timestamp = static_cast<int64_t>(pos * AV_TIME_BASE);
    av_seek_frame(format_ctx_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    SPDLOG_DEBUG("DemuxNode: seek to {:.2f}s", pos);
    return true;
}

bool DemuxNode::HasPendingSeek() const {
    return pending_seek_.load(std::memory_order_acquire) != kNoSeekPending;
}

void DemuxNode::RefreshLocalSerial() {
    // Latch once per reposition. Reading the epoch per packet would stamp a
    // packet read before the seek with the post-seek epoch, defeating the
    // staleness check.
    if (graph_) {
        local_serial_ = graph_->SeekEpoch();
    }
}

void DemuxNode::EmitEos(OutputPort* video_port, OutputPort* audio_port) {
    if (video_port && video_port->IsConnected()) {
        video_port->Push(MediaBuffer::MakeEos(local_serial_));
    }
    if (audio_port && audio_port->IsConnected()) {
        audio_port->Push(MediaBuffer::MakeEos(local_serial_));
    }
}

void DemuxNode::RoutePacket(AVPacketPtr pkt, OutputPort* video_port,
                            OutputPort* audio_port) {
    int stream_index = pkt->stream_index;
    OutputPort* target = nullptr;
    if (stream_index == video_stream_index_ && video_port) {
        target = video_port;
    } else if (stream_index == audio_stream_index_ && audio_port) {
        target = audio_port;
    }
    if (!target || !target->IsConnected()) {
        return;
    }

    auto* stream = format_ctx_->streams[stream_index];
    Timestamp ts;
    ts.pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts * av_q2d(stream->time_base)
                                          : 0.0;
    ts.time_base = {stream->time_base.num, stream->time_base.den};

    BufferFlags flags = BufferFlags::kNone;
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        flags = flags | BufferFlags::kKeyFrame;
    }
    SPDLOG_DEBUG("DemuxNode: route stream={} -> {} serial={} pts={:.3f}",
                 stream_index, target == video_port ? "video" : "audio",
                 local_serial_, ts.pts);
    MediaBuffer buf(std::move(pkt), ts, flags);
    buf.set_serial(local_serial_);
    target->Push(std::move(buf));
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
        if (HandlePendingSeek()) {
            RefreshLocalSerial();
        }

        AVPacketPtr pkt;
        int ret = av_read_frame(format_ctx_, pkt.get());
        SPDLOG_DEBUG("DemuxNode: av_read_frame ret={} stream={} serial={}",
                     ret, pkt.get() ? pkt->stream_index : -1, local_serial_);

        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(format_ctx_->pb)) {
                EmitEos(video_port, audio_port);
            } else {
                SPDLOG_WARN("DemuxNode: av_read_frame error {}", ret);
            }
            // Wait for a seek request or stop.
            while (running_.load(std::memory_order_relaxed) &&
                   !HasPendingSeek()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        RoutePacket(std::move(pkt), video_port, audio_port);
    }
}

}  // namespace mvp::graph
