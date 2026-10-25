#include "nodes/decoder_node.h"

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
#include <spdlog/spdlog.h>

#include "base/ffmpeg_utils.h"
#include "base/media_frame.h"
#include "gpu/gpu_device.h"
#include "gpu/pixel_format_map.h"
#include "graph/media_graph.h"

namespace mvp::graph {

namespace {

// Frames held downstream at once (link depth + the sink's current frame) must
// not starve the decoder's fixed surface pool (mpv hwdec-extra-frames).
constexpr int kExtraHwFrames = 6;

// Builds the decoder surface pool here instead of letting FFmpeg do it, so the
// GPU device can refine it (e.g. shader-readable surfaces for GPU passes).
// Must run inside get_format (avcodec_get_hw_frames_parameters contract).
bool InitHwFramesContext(AVCodecContext* ctx, gpu::GpuDevice* device,
                         AVPixelFormat hw_format) {
    AVBufferRef* params = nullptr;
    if (avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, hw_format, &params) < 0) {
        return false;
    }
    AVBufferRefPtr frames(params);
    auto* frames_ctx = reinterpret_cast<AVHWFramesContext*>(frames->data);
    if (frames_ctx->initial_pool_size > 0) {
        // FFmpeg's automatic path guarantees 4 work surfaces; the parameters above hold 1.
        frames_ctx->initial_pool_size += 3;
    }
    device->RefineDecoderFrames(frames_ctx);
    if (av_hwframe_ctx_init(frames.get()) < 0) {
        return false;
    }
    av_buffer_unref(&ctx->hw_frames_ctx);
    ctx->hw_frames_ctx = frames.release();
    return true;
}

}  // namespace

DecoderNode::DecoderNode(bool allow_hardware) : allow_hardware_(allow_hardware) {
    input_port_ = std::make_unique<InputPort>(this);
    output_port_ = std::make_unique<OutputPort>(this);
}

DecoderNode::~DecoderNode() {
    Stop();
    CloseCodec();
}

bool DecoderNode::Negotiate() {
    // --- Pure format reasoning (no resource allocation) ---
    // Read the upstream EncodedFormat, then derive this node's output format
    // directly from AVCodecParameters (which carries width/height/sample_rate)
    // WITHOUT opening the codec. Resource allocation happens in Prepare().
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("DecoderNode: input port not connected");
        return false;
    }
    const MediaFormat& fmt = input_port_->Format();
    if (!fmt.IsEncoded() || !fmt.AsEncoded().codec_params) {
        SPDLOG_ERROR("DecoderNode: input is not an encoded format with params");
        return false;
    }

    const auto& enc = fmt.AsEncoded();
    negotiated_codecpar_ = enc.codec_params.get();
    time_base_ = {fmt.time_base().num, fmt.time_base().den};
    media_type_ = fmt.media_type();

    // Derive output format from codec params (no codec open). Pixel format
    // may be a hardware domain when the downstream chain accepts one; the
    // real format is refined at runtime once frames arrive.
    if (media_type_ == MediaType::kVideo) {
        name_ = "DecoderNode(video)";
        frame_rate_ = enc.frame_rate;
        const AVCodec* codec =
            avcodec_find_decoder(negotiated_codecpar_->codec_id);
        output_port_->SetFormat(MediaFormat::Video(
            negotiated_codecpar_->width, negotiated_codecpar_->height,
            PickOutputPixelFormat(codec), frame_rate_));
    } else if (media_type_ == MediaType::kAudio) {
        name_ = "DecoderNode(audio)";
        output_port_->SetFormat(MediaFormat::Audio(
            negotiated_codecpar_->sample_rate,
            negotiated_codecpar_->ch_layout.nb_channels, SampleFormat::kFloat));
    }
    return true;
}

PixelFormat DecoderNode::PickOutputPixelFormat(const AVCodec* codec) const {
    // Downstream suggests, upstream decides: a hardware domain is only
    // negotiated when the device exists, the codec supports it, and the
    // immediate downstream accepts it. Software otherwise.
    if (!output_port_->IsConnected()) {
        return PixelFormat::kYUV420P;
    }
    gpu::GpuDevice* device = graph_ ? graph_->GpuDevice() : nullptr;
    if (!allow_hardware_ || !device || !codec || !device->SupportsDecoder(codec)) {
        return PixelFormat::kYUV420P;
    }
    const FormatCaps& peer = output_port_->Peer()->Caps();
    PixelFormat domain = device->Domain();
    bool accepted = peer.pixel_formats.empty() ||
                    std::find(peer.pixel_formats.begin(),
                              peer.pixel_formats.end(),
                              domain) != peer.pixel_formats.end();
    return accepted ? domain : PixelFormat::kYUV420P;
}

bool DecoderNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;  // Already prepared
    }
    if (state_ != NodeState::kConfigured && state_ != NodeState::kIdle) {
        SPDLOG_ERROR("DecoderNode: Prepare called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    if (!negotiated_codecpar_) {
        SPDLOG_ERROR("DecoderNode: no codec params from negotiation");
        state_ = NodeState::kError;
        return false;
    }

    gpu_device_ = graph_ ? graph_->GpuDevice() : nullptr;
    if (!FindAndOpenCodec(negotiated_codecpar_)) {
        state_ = NodeState::kError;
        return false;
    }

    state_ = NodeState::kPrepared;
    return true;
}

bool DecoderNode::FindAndOpenCodec(const AVCodecParameters* codecpar) {
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        SPDLOG_ERROR("DecoderNode: codec not found for id {}",
                     static_cast<int>(codecpar->codec_id));
        return false;
    }

    const MediaFormat& out = output_port_->Format();
    bool want_hw = gpu_device_ && media_type_ == MediaType::kVideo &&
                   out.IsVideo() &&
                   out.AsVideo().pixel_format == gpu_device_->Domain();
    codec_name_ = codec->name;
    hardware_active_ = want_hw && TryOpenCodec(codec, codecpar, /*use_hw=*/true);
    if (hardware_active_) {
        SPDLOG_INFO("DecoderNode: hardware decode enabled ({})", codec->name);
        return true;
    }
    if (want_hw) {
        SPDLOG_WARN("DecoderNode: hardware decode failed for '{}', "
                    "retrying software",
                    codec->name);
    }
    SPDLOG_INFO("{}: software decode ({})", name_, codec->name);
    return TryOpenCodec(codec, codecpar, /*use_hw=*/false);
}

bool DecoderNode::TryOpenCodec(const AVCodec* codec,
                               const AVCodecParameters* codecpar, bool use_hw) {
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, codecpar) < 0) {
        SPDLOG_ERROR("DecoderNode: failed to copy codec params");
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    if (use_hw) {
        // FFmpeg picks the actual format at open via GetFormat; `opaque`
        // must outlive codec_ctx_ — the device is graph-owned.
        codec_ctx_->opaque = gpu_device_;
        codec_ctx_->get_format = &DecoderNode::GetFormat;
        codec_ctx_->hw_device_ctx = av_buffer_ref(gpu_device_->DeviceRef());
        codec_ctx_->extra_hw_frames = kExtraHwFrames;
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        SPDLOG_ERROR("DecoderNode: failed to open codec '{}'", codec->name);
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    return true;
}

AVPixelFormat DecoderNode::GetFormat(AVCodecContext* ctx,
                                     const AVPixelFormat* pix_fmts) {
    auto* device = static_cast<gpu::GpuDevice*>(ctx->opaque);
    AVPixelFormat hw = gpu::ToAvPixelFormat(device->Domain());
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p != hw) {
            continue;
        }
        if (!InitHwFramesContext(ctx, device, hw)) {
            SPDLOG_WARN("DecoderNode: refined hw frame pool failed, using FFmpeg's default");
        }
        return hw;
    }
    return pix_fmts[0];  // Hardware not offered: decode in software.
}

bool DecoderNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("DecoderNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    running_ = true;
    decode_thread_ = std::thread(&DecoderNode::DecodeLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void DecoderNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }

    running_ = false;
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }

    CloseCodec();
    state_ = NodeState::kIdle;
}

void DecoderNode::Flush() {
    // Do NOT call avcodec_flush_buffers here — codec_ctx_ is owned by the
    // decode thread. Cross-thread access causes use-after-free crashes.
    // Instead, the decode thread notices the new seek epoch on the next
    // packet it pulls and flushes the codec on its own thread
    // (MaybeFlushOnSerialChange). The drop target arrives afterwards through
    // OnCommand(kSeek).
}

void DecoderNode::SetDropUntilPts(double pts) {
    // codec_ctx_ is decode-thread-owned; the decode thread applies the target
    // (and skip_frame) itself in MaybeFlushOnSerialChange.
    std::lock_guard lock(drop_target_mutex_);
    drop_target_ = {graph_ ? graph_->SeekEpoch() : 0, pts};
}

void DecoderNode::OnCommand(const Command& cmd) {
    // On seek, drop decoded frames until the target PTS (fast catch-up).
    // The codec flush itself happens on the decode thread via serial change.
    if (cmd.type == CommandType::kSeek) {
        SetDropUntilPts(cmd.position);
    }
}

std::vector<InputPort*> DecoderNode::Inputs() {
    return {input_port_.get()};
}

std::vector<OutputPort*> DecoderNode::Outputs() {
    return {output_port_.get()};
}

void DecoderNode::CloseCodec() {
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
}

void DecoderNode::DrainFrames() {
    // FFmpeg locks the shared device context internally (the device handed it
    // its lock), so no app-level lock here: Push may block on a full link.
    while (running_.load(std::memory_order_relaxed)) {
        AVFramePtr frame;
        int ret = avcodec_receive_frame(codec_ctx_, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            SPDLOG_WARN("{}: receive_frame failed: {}", name_, AvErrorString(ret));
            break;
        }

        double frame_pts = (frame->pts != AV_NOPTS_VALUE)
                               ? frame->pts * av_q2d(time_base_)
                               : 0.0;
        if (DropForSeek(frame_pts)) {
            continue;
        }
        MaybeAnnounceFormat(frame.get());

        Timestamp ts;
        ts.pts = frame_pts;
        ts.time_base = {time_base_.num, time_base_.den};
        MediaBuffer buf(MediaFrame(frame.get()), ts);
        buf.set_serial(current_serial_);
        output_port_->Push(std::move(buf));
    }
}

bool DecoderNode::DropForSeek(double frame_pts) {
    if (seek_target_ == kNoDropTarget) {
        return false;
    }
    if (frame_pts < seek_target_) {
        return true;
    }
    codec_ctx_->skip_frame = AVDISCARD_DEFAULT;
    seek_target_ = kNoDropTarget;
    SPDLOG_DEBUG("{}: drop cleared at pts={:.3f}", name_, frame_pts);
    return false;
}

void DecoderNode::MaybeAnnounceFormat(const AVFrame* frame) {
    // FFmpeg's get_format rebuilds the hw decoder + surface pool after every
    // flush; the old pool dies with the last frame still referencing it.
    if (frame->hw_frames_ctx && frame->hw_frames_ctx->data != announced_frames_ctx_) {
        if (announced_frames_ctx_) {
            SPDLOG_INFO("{}: hw frames context replaced (old={}, new={})", name_,
                        announced_frames_ctx_,
                        static_cast<const void*>(frame->hw_frames_ctx->data));
        }
        announced_frames_ctx_ = frame->hw_frames_ctx->data;
    }
    // Audio frames use the same AVFrame::format field for sample formats;
    // this correction applies to video only.
    if (media_type_ != MediaType::kVideo || frame->format == announced_av_format_) {
        return;
    }
    announced_av_format_ = frame->format;

    PixelFormat pf = gpu::FromAvPixelFormat(frame->format);
    PixelFormat sw = PixelFormat::kUnknown;
    int pool_init = -1;
    if (frame->hw_frames_ctx) {
        auto* fctx =
            reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        sw = fctx ? gpu::FromAvPixelFormat(fctx->sw_format)
                  : PixelFormat::kUnknown;
        pool_init = fctx ? fctx->initial_pool_size : -1;
    }
    SPDLOG_INFO("DecoderNode: actual output format av={} domain={} sw={} pool_init={}",
                frame->format, static_cast<int>(pf), static_cast<int>(sw),
                pool_init);
    output_port_->SetFormat(MediaFormat::Video(frame->width, frame->height, pf,
                                               frame_rate_, sw));
}

void DecoderNode::MaybeFlushOnSerialChange(int serial) {
    // After a seek the graph bumps its epoch. When the epoch changes, flush
    // the codec on THIS thread (safe) to clear stale reference frames before
    // decoding the new (post-seek) packets.
    if (serial == last_serial_) {
        return;
    }
    avcodec_flush_buffers(codec_ctx_);
    last_serial_ = serial;
    seek_target_ = DropTargetFor(serial);
    // AVDISCARD_NONREF accelerates software catch-up by skipping non-ref
    // frames. Hardware decode keeps AVDISCARD_DEFAULT: NONREF is not yet
    // validated with D3D11VA (see docs/improvements/seek-performance.md).
    // Assigned on every epoch: a catch-up that never reached its target
    // (seek past the last frame) must not keep discarding frames afterwards.
    const bool catch_up = seek_target_ != kNoDropTarget && codec_ctx_->hw_device_ctx == nullptr;
    codec_ctx_->skip_frame = catch_up ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
}

double DecoderNode::DropTargetFor(int serial) {
    std::lock_guard lock(drop_target_mutex_);
    return drop_target_.epoch == serial ? drop_target_.pts : kNoDropTarget;
}

int DecoderNode::SendPacket(const AVPacket* pkt) {
    int ret = avcodec_send_packet(codec_ctx_, pkt);
    if (ret == AVERROR(EAGAIN)) {
        // The decoder takes no input until its pending output is drained.
        DrainFrames();
        ret = avcodec_send_packet(codec_ctx_, pkt);
    }
    return ret;
}

void DecoderNode::HandleEos() {
    // Drain remaining frames, then propagate EOS downstream.
    const int ret = SendPacket(nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        SPDLOG_WARN("{}: entering drain mode failed: {}", name_, AvErrorString(ret));
    }
    DrainFrames();
    output_port_->Push(MediaBuffer::MakeEos(current_serial_));
}

void DecoderNode::ProcessPacket(MediaBuffer& buf) {
    if (!buf.IsPacket()) {
        return;  // Unexpected buffer type
    }
    AVPacketPtr& pkt = buf.AsPacket();
    // A null/empty packet is a drain request.
    AVPacket* to_send = (pkt.get() && pkt->data) ? pkt.get() : nullptr;
    const int ret = SendPacket(to_send);
    if (to_send && ret < 0) {
        // Corrupt input: skip the packet and keep decoding (ffplay/mpv do too).
        SPDLOG_WARN("{}: send_packet failed, packet dropped: {}", name_, AvErrorString(ret));
    }
    DrainFrames();
}

void DecoderNode::DecodeLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        current_serial_ = buf.serial();
        MaybeFlushOnSerialChange(buf.serial());

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            HandleEos();
            // After EOS, loop back to a blocking Pull. New data only arrives
            // after a seek (which carries a new serial, handled above). The
            // next iteration processes it normally via a clean control flow.
            continue;
        }

        ProcessPacket(buf);
    }
}

}  // namespace mvp::graph
