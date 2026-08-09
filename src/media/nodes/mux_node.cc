#include "nodes/mux_node.h"

#include <cmath>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "mvp/container_probe.h"

namespace mvp::graph {

MuxNode::MuxNode(std::string output_path, bool has_video, bool has_audio)
    : output_path_(std::move(output_path)) {
    if (has_video) {
        StreamSlot slot;
        slot.port = std::make_unique<InputPort>(this);
        slot.media_type = MediaType::kVideo;
        slot.primary = true;
        slots_.push_back(std::move(slot));
    }
    if (has_audio) {
        StreamSlot slot;
        slot.port = std::make_unique<InputPort>(this);
        slot.media_type = MediaType::kAudio;
        slot.primary = !has_video;  // audio is primary only when there is no video
        slots_.push_back(std::move(slot));
    }
}

void MuxNode::ResolveOutputRequirements() {
    const AVOutputFormat* fmt =
        av_guess_format(nullptr, output_path_.c_str(), nullptr);
    if (!fmt) {
        SPDLOG_WARN("MuxNode: cannot guess output format for '{}'",
                    output_path_);
        needs_global_header_ = false;
        return;
    }
    needs_global_header_ = (fmt->flags & AVFMT_GLOBALHEADER) != 0;
    SPDLOG_INFO("MuxNode: output format '{}' needs_global_header={}", fmt->name,
                needs_global_header_);
}

void MuxNode::DeclareCaps() {
    ResolveOutputRequirements();
    mvp::ContainerCodecCaps codec_caps = mvp::ContainerProbe::Query(output_path_);
    for (auto& slot : slots_) {
        FormatCaps caps;
        caps.media_type = slot.media_type;
        caps.header_placement = needs_global_header_ ? HeaderPlacement::kGlobal
                                                     : HeaderPlacement::kInBand;
        const std::vector<std::string>& names = (slot.media_type == MediaType::kVideo)
                                                     ? codec_caps.video_codecs
                                                     : codec_caps.audio_codecs;
        for (const std::string& name : names) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
            if (codec) caps.codec_ids.push_back(codec->id);
        }
        SPDLOG_DEBUG("MuxNode: container declares {} supported codec(s) for {} port",
                     caps.codec_ids.size(),
                     slot.media_type == MediaType::kVideo ? "video" : "audio");
        slot.port->SetCaps(std::move(caps));
    }
}

MuxNode::~MuxNode() {
    Stop();
    CloseOutput();
}

bool MuxNode::Negotiate() {
    for (auto& slot : slots_) {
        if (!slot.port->IsConnected()) {
            SPDLOG_ERROR("MuxNode: input port not connected");
            return false;
        }
    }
    return true;
}

bool MuxNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;  // Already prepared
    }
    if (state_ != NodeState::kConfigured && state_ != NodeState::kIdle) {
        SPDLOG_ERROR("MuxNode: Prepare called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    if (!OpenOutput()) {
        CloseOutput();
        state_ = NodeState::kError;
        return false;
    }

    state_ = NodeState::kPrepared;
    return true;
}

bool MuxNode::OpenOutput() {
    int ret = avformat_alloc_output_context2(&format_ctx_, nullptr, nullptr,
                                             output_path_.c_str());
    if (ret < 0 || !format_ctx_) {
        SPDLOG_ERROR("MuxNode: failed to infer output format for '{}'", output_path_);
        return false;
    }
    if (!CreateStreams()) {
        return false;
    }
    if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&format_ctx_->pb, output_path_.c_str(), AVIO_FLAG_WRITE) < 0) {
            SPDLOG_ERROR("MuxNode: failed to open output file '{}'", output_path_);
            return false;
        }
    }
    int wh_ret = avformat_write_header(format_ctx_, nullptr);
    if (wh_ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(wh_ret, errbuf, sizeof(errbuf));
        SPDLOG_ERROR("MuxNode: avformat_write_header failed for '{}' (err {}: {})",
                     output_path_, wh_ret, errbuf);
        for (auto& slot : slots_) {
            if (slot.av_stream) {
                auto* cp = slot.av_stream->codecpar;
                SPDLOG_ERROR("  stream[{}] codec_id={} type={} extradata_size={} "
                             "w={} h={} sr={} ch={}",
                             slot.av_stream->index,
                             static_cast<int>(cp->codec_id),
                             static_cast<int>(cp->codec_type),
                             cp->extradata_size, cp->width, cp->height,
                             cp->sample_rate, cp->ch_layout.nb_channels);
            }
        }
        return false;
    }
    header_written_ = true;
    return true;
}

bool MuxNode::CreateStreams() {
    for (auto& slot : slots_) {
        AVStream* stream = avformat_new_stream(format_ctx_, nullptr);
        if (!stream) {
            SPDLOG_ERROR("MuxNode: avformat_new_stream failed");
            return false;
        }
        const MediaFormat& fmt = slot.port->Format();
        if (!fmt.IsEncoded() || !fmt.AsEncoded().codec_params) {
            SPDLOG_ERROR("MuxNode: upstream port has no encoded format/params");
            return false;
        }
        avcodec_parameters_copy(stream->codecpar, fmt.AsEncoded().codec_params.get());
        stream->time_base = {fmt.time_base().num, fmt.time_base().den};
        slot.av_stream = stream;
    }
    return true;
}

bool MuxNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("MuxNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    running_ = true;
    mux_thread_ = std::thread(&MuxNode::MuxLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void MuxNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }

    running_ = false;
    if (mux_thread_.joinable()) {
        mux_thread_.join();
    }

    CloseOutput();
    state_ = NodeState::kIdle;
}

void MuxNode::Flush() {
    // Transcoder v1 has no seek support; nothing to flush.
}

std::vector<InputPort*> MuxNode::Inputs() {
    std::vector<InputPort*> result;
    result.reserve(slots_.size());
    for (auto& slot : slots_) {
        result.push_back(slot.port.get());
    }
    return result;
}

void MuxNode::CloseOutput() {
    if (format_ctx_) {
        if (format_ctx_->pb && !(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx_->pb);
        }
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
    }
    header_written_ = false;
    for (auto& slot : slots_) {
        slot.av_stream = nullptr;
        slot.eos = false;
    }
}

bool MuxNode::AllSlotsAtEos() const {
    for (const auto& slot : slots_) {
        if (!slot.eos) {
            return false;
        }
    }
    return true;
}

void MuxNode::FillPendingSlots(PendingSlots& pending) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (pending[i] || slots_[i].eos) {
            continue;
        }
        auto opt = slots_[i].port->Pull();
        if (!opt) {
            slots_[i].eos = true;  // link aborted
            continue;
        }
        if (HasFlag(opt->flags(), BufferFlags::kEos)) {
            slots_[i].eos = true;
            continue;
        }
        pending[i] = std::move(opt);
    }
}

int MuxNode::PickNextSlot(const PendingSlots& pending) const {
    int best = -1;
    double best_pts = 0.0;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (!pending[i]) {
            continue;
        }
        double pts = pending[i]->timestamp().pts;
        if (best < 0 || pts < best_pts) {
            best = static_cast<int>(i);
            best_pts = pts;
        }
    }
    return best;
}

void MuxNode::WriteSlotPacket(StreamSlot& slot, MediaBuffer& buf) {
    if (!buf.IsPacket() || !slot.av_stream) {
        return;
    }
    AVPacketPtr& pkt = buf.AsPacket();
    pkt->stream_index = slot.av_stream->index;

    Timestamp ts = buf.timestamp();
    AVRational src_tb{ts.time_base.num, ts.time_base.den};
    av_packet_rescale_ts(pkt.get(), src_tb, slot.av_stream->time_base);

    int ret = av_interleaved_write_frame(format_ctx_, pkt.get());
    if (ret < 0) {
        SPDLOG_ERROR("MuxNode: av_interleaved_write_frame failed (err {})", ret);
        state_ = NodeState::kError;
        return;
    }

    if (slot.primary && progress_hook_) {
        progress_hook_(ts.pts);
    }
}

void MuxNode::DrainPending(PendingSlots& pending) {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (pending[i]) {
            WriteSlotPacket(slots_[i], *pending[i]);
            pending[i].reset();
        }
    }
}

void MuxNode::FinalizeOutput() {
    if (header_written_ && format_ctx_) {
        av_write_trailer(format_ctx_);
    }
    if (graph_) {
        graph_->ReportEvent(GraphEvent::kEos);
    }
}

void MuxNode::MuxLoop() {
    PendingSlots pending(slots_.size());

    while (running_.load(std::memory_order_relaxed) && !AllSlotsAtEos()) {
        FillPendingSlots(pending);
        if (AllSlotsAtEos()) {
            break;
        }
        int best = PickNextSlot(pending);
        if (best < 0) {
            continue;
        }
        WriteSlotPacket(slots_[best], *pending[best]);
        pending[best].reset();
    }

    DrainPending(pending);
    FinalizeOutput();
}

}  // namespace mvp::graph
