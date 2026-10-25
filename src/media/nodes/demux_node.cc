#include "nodes/demux_node.h"

#include <cstdint>
#include <cstdlib>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "graph/graph_command.h"
#include "graph/media_format.h"
#include "graph/media_graph.h"

namespace mvp::graph {

namespace {

// Transient read errors are retried this many times in a row before the
// source is declared broken (ffplay keeps retrying forever; a bounded retry
// lets the application surface the error instead of a silent stall).
constexpr int kMaxConsecutiveReadErrors = 10;
constexpr std::chrono::milliseconds kReadRetryDelay{10};

// ffmpeg's -dts_delta_threshold default: forward jumps beyond this are
// discontinuities; backward jumps over 0.1s always are.
constexpr int64_t kDtsDeltaThresholdUs = 10 * int64_t{AV_TIME_BASE};

void ShiftTimestamps(AVPacket* pkt, int64_t delta) {
    if (pkt->pts != AV_NOPTS_VALUE) {
        pkt->pts += delta;
    }
    if (pkt->dts != AV_NOPTS_VALUE) {
        pkt->dts += delta;
    }
}

// Moves a packet onto the zero-based pipeline timeline.
void RebaseTimestamps(AVPacket* pkt, AVRational time_base, int64_t offset_us) {
    if (offset_us != 0) {
        ShiftTimestamps(pkt, -av_rescale_q(offset_us, av_get_time_base_q(), time_base));
    }
}

}  // namespace

DemuxNode::DemuxNode(std::string file_path, int video_stream_idx,
                     int audio_stream_idx, DemuxOptions options)
    : file_path_(std::move(file_path)),
      options_(options),
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
    SPDLOG_INFO("DemuxNode: opened '{}' - duration {:.2f}s, start {:.3f}s, video={}, audio={}",
                file_path_, Duration(), static_cast<double>(start_offset_us_) / AV_TIME_BASE,
                video_stream_index_, audio_stream_index_);
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
        output_ports_[0]->SetFormat(MakeStreamFormat(video_stream_index_, MediaType::kVideo,
                                                     {s->avg_frame_rate.num, s->avg_frame_rate.den}));
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
    format_ctx_ = avformat_alloc_context();
    if (!format_ctx_) {
        SPDLOG_ERROR("DemuxNode: avformat_alloc_context failed");
        return false;
    }
    // Lets Stop() abort IO that blocks inside FFmpeg (stalled network input).
    format_ctx_->interrupt_callback = {&DemuxNode::InterruptCallback, this};
    int ret = avformat_open_input(&format_ctx_, file_path_.c_str(), nullptr, nullptr);
    if (ret < 0) {
        // avformat_open_input frees a caller-allocated context on failure.
        SPDLOG_ERROR("DemuxNode: failed to open '{}': {}", file_path_, AvErrorString(ret));
        return false;
    }
    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0) {
        SPDLOG_ERROR("DemuxNode: failed to find stream info for '{}': {}", file_path_,
                     AvErrorString(ret));
        avformat_close_input(&format_ctx_);
        return false;
    }
    start_offset_us_ = (format_ctx_->start_time != AV_NOPTS_VALUE) ? format_ctx_->start_time : 0;
    return true;
}

int DemuxNode::InterruptCallback(void* opaque) {
    return static_cast<DemuxNode*>(opaque)->abort_io_.load(std::memory_order_acquire) ? 1 : 0;
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

    abort_io_.store(false, std::memory_order_release);
    running_ = true;
    // A seek requested before Start stays pending (the loop executes it
    // first); packets are stamped with the epoch current right now.
    RefreshLocalSerial();
    demux_thread_ = std::thread(&DemuxNode::DemuxLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void DemuxNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }

    {
        std::lock_guard lock(wake_mutex_);
        running_ = false;
    }
    abort_io_.store(true, std::memory_order_release);
    wake_cv_.notify_all();
    if (demux_thread_.joinable()) {
        demux_thread_.join();
    }
    state_ = NodeState::kIdle;
}

void DemuxNode::Flush() {
    // Reposition happens in OnCommand(kSeek) -> RequestSeek() instead.
}

void DemuxNode::RequestSeek(double position_seconds) {
    {
        std::lock_guard lock(wake_mutex_);
        pending_seek_.store(position_seconds, std::memory_order_release);
    }
    wake_cv_.notify_one();
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
    // Positions live on the rebased timeline; the container's is offset.
    const int64_t target = static_cast<int64_t>(pos * AV_TIME_BASE) + start_offset_us_;
    // Nearest keyframe at or before the target; decoders drop up to it.
    const int ret = avformat_seek_file(format_ctx_, -1, INT64_MIN, target, target, 0);
    if (ret < 0) {
        SPDLOG_WARN("DemuxNode: seek to {:.2f}s failed: {}", pos, AvErrorString(ret));
    } else {
        SPDLOG_DEBUG("DemuxNode: seek to {:.2f}s", pos);
    }
    // Raw timestamps again from here: seek targets are in raw time.
    discont_offset_us_ = 0;
    video_dts_ = DtsTrack{};
    audio_dts_ = DtsTrack{};
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

void DemuxNode::PostError(const std::string& detail) {
    if (graph_) {
        graph_->Bus().Post({GraphMessageType::kError, this, local_serial_, detail});
    }
}

bool DemuxNode::RoutePacket(AVPacketPtr pkt, OutputPort* video_port,
                            OutputPort* audio_port) {
    int stream_index = pkt->stream_index;
    OutputPort* target = nullptr;
    if (stream_index == video_stream_index_ && video_port) {
        target = video_port;
    } else if (stream_index == audio_stream_index_ && audio_port) {
        target = audio_port;
    }
    if (!target || !target->IsConnected()) {
        return true;
    }

    auto* stream = format_ctx_->streams[stream_index];
    RebaseTimestamps(pkt.get(), stream->time_base, start_offset_us_);
    if (options_.correct_ts_discontinuities && (format_ctx_->iformat->flags & AVFMT_TS_DISCONT)) {
        CorrectDiscontinuity(pkt.get(), stream_index,
                             target == video_port ? video_dts_ : audio_dts_);
    }
    Timestamp ts;
    ts.pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts * av_q2d(stream->time_base)
                                          : 0.0;
    ts.time_base = {stream->time_base.num, stream->time_base.den};

    BufferFlags flags = BufferFlags::kNone;
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        flags = flags | BufferFlags::kKeyFrame;
    }
    MediaBuffer buf(std::move(pkt), ts, flags);
    buf.set_serial(local_serial_);
    return target->Push(std::move(buf));
}

// ffmpeg's ts_discontinuity_detect: a stream whose DTS jumps backwards (or
// far forward) moves every later packet by the jump, so the output timeline
// stays continuous across concatenated or restarted transport streams.
void DemuxNode::CorrectDiscontinuity(AVPacket* pkt, int stream_index, DtsTrack& track) {
    if (pkt->dts == AV_NOPTS_VALUE) {
        return;
    }
    const AVRational tb = format_ctx_->streams[stream_index]->time_base;
    ShiftTimestamps(pkt, av_rescale_q(discont_offset_us_, av_get_time_base_q(), tb));
    const int64_t dts_us = av_rescale_q(pkt->dts, tb, av_get_time_base_q());
    if (track.next_us != kNoDts) {
        const int64_t delta = dts_us - track.next_us;
        if (std::llabs(delta) > kDtsDeltaThresholdUs || dts_us + AV_TIME_BASE / 10 < track.last_us) {
            discont_offset_us_ -= delta;
            ShiftTimestamps(pkt, -av_rescale_q(delta, av_get_time_base_q(), tb));
            SPDLOG_WARN("DemuxNode: timestamp discontinuity of {:.3f}s in stream {} folded away",
                        static_cast<double>(delta) / AV_TIME_BASE, stream_index);
        }
    }
    track.last_us = av_rescale_q(pkt->dts, tb, av_get_time_base_q());
    const int64_t duration_us =
        pkt->duration > 0 ? av_rescale_q(pkt->duration, tb, av_get_time_base_q()) : 0;
    track.next_us = track.last_us + duration_us;
}

std::pair<OutputPort*, OutputPort*> DemuxNode::StreamPorts() const {
    // Port index mapping: video port is always [0] if exists, audio follows.
    OutputPort* video_port = nullptr;
    OutputPort* audio_port = nullptr;
    size_t port_idx = 0;
    if (video_stream_index_ >= 0) {
        video_port = output_ports_[port_idx++].get();
    }
    if (audio_stream_index_ >= 0) {
        audio_port = output_ports_[port_idx++].get();
    }
    return {video_port, audio_port};
}

void DemuxNode::WaitForWakeup(std::optional<std::chrono::milliseconds> timeout) {
    std::unique_lock lock(wake_mutex_);
    auto woken = [this] {
        return !running_.load(std::memory_order_relaxed) || HasPendingSeek();
    };
    if (timeout) {
        wake_cv_.wait_for(lock, *timeout, woken);
    } else {
        wake_cv_.wait(lock, woken);
    }
}

void DemuxNode::HandleReadFailure(int ret, int& read_errors, OutputPort* video_port,
                                  OutputPort* audio_port) {
    // avio flags EOF on IO errors too; a set pb->error tells them apart.
    const bool io_error = format_ctx_->pb && format_ctx_->pb->error < 0;
    if (!io_error && (ret == AVERROR_EOF || avio_feof(format_ctx_->pb))) {
        EmitEos(video_port, audio_port);
        WaitForWakeup(std::nullopt);  // Sleep until a seek or Stop, no polling.
        return;
    }
    if (!io_error && ret == AVERROR(EAGAIN)) {
        WaitForWakeup(kReadRetryDelay);  // Live source has no data yet.
        return;
    }
    if (!io_error && ++read_errors < kMaxConsecutiveReadErrors) {
        SPDLOG_WARN("DemuxNode: read error ({}/{}), retrying: {}", read_errors,
                    kMaxConsecutiveReadErrors, AvErrorString(ret));
        WaitForWakeup(kReadRetryDelay);
        return;
    }
    SPDLOG_ERROR("DemuxNode: reading '{}' failed: {}", file_path_, AvErrorString(ret));
    PostError("read failed: " + AvErrorString(ret));
    read_errors = 0;
    WaitForWakeup(std::nullopt);  // A seek may still land on readable data.
}

void DemuxNode::DemuxLoop() {
    const auto [video_port, audio_port] = StreamPorts();
    int read_errors = 0;
    while (running_.load(std::memory_order_relaxed)) {
        if (HandlePendingSeek()) {
            RefreshLocalSerial();
            read_errors = 0;
        }

        AVPacketPtr pkt;
        const int ret = av_read_frame(format_ctx_, pkt.get());
        if (ret >= 0) {
            read_errors = 0;
            if (!RoutePacket(std::move(pkt), video_port, audio_port)) {
                break;  // Downstream link aborted: the graph is stopping.
            }
        } else if (running_.load(std::memory_order_relaxed)) {
            // Not interrupted by Stop(): EOF or a genuine read failure.
            HandleReadFailure(ret, read_errors, video_port, audio_port);
        }
    }
}

}  // namespace mvp::graph
