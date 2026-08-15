#include "nodes/decoder_node.h"

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "gpu/gpu_device.h"
#include "gpu/pixel_format_map.h"
#include "graph/media_graph.h"
#include "media_frame.h"

namespace mvp::graph {

DecoderNode::DecoderNode() {
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
    if (!device || !codec || !device->SupportsDecoder(codec)) {
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
    if (want_hw && TryOpenCodec(codec, codecpar, /*use_hw=*/true)) {
        SPDLOG_INFO("DecoderNode: hardware decode enabled ({})", codec->name);
        return true;
    }
    if (want_hw) {
        SPDLOG_WARN("DecoderNode: hardware decode failed for '{}', "
                    "retrying software",
                    codec->name);
    }
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
        if (*p == hw) {
            return hw;
        }
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
    // Instead, the decode thread detects serial change via Link serial stamp
    // and flushes the codec on its own thread (see DecodeLoop).
    //
    // drop_until_pts_ is preserved — it was set by SetDropUntilPts() before
    // this Flush() call (that's the correct ordering in MediaPlayer::Seek).
}

void DecoderNode::SetDropUntilPts(double pts) {
    // codec_ctx_ is decode-thread-owned; skip_frame is set there instead,
    // in MaybeFlushOnSerialChange.
    drop_until_pts_.store(pts, std::memory_order_release);
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
    while (running_.load(std::memory_order_relaxed)) {
        AVFramePtr frame;
        int ret = avcodec_receive_frame(codec_ctx_, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            SPDLOG_WARN("DecoderNode: receive_frame error {}", ret);
            break;
        }

        double frame_pts = (frame->pts != AV_NOPTS_VALUE)
                               ? frame->pts * av_q2d(time_base_)
                               : 0.0;

        // Seek optimization: drop frames before target PTS
        double target = drop_until_pts_.load(std::memory_order_acquire);
        if (target > 0.0 && frame_pts < target) {
            continue;
        }

        // Reached target: restore normal decode
        if (target > 0.0) {
            codec_ctx_->skip_frame = AVDISCARD_DEFAULT;
            drop_until_pts_.store(0.0, std::memory_order_release);
        }

        MediaFrame mf(frame.get());
        if (frame->hw_frames_ctx && gpu_device_) {
            void* tex = gpu_device_->CopyForPresentation(frame.get());
            if (tex) {
                mf.SetHwPresentationTexture(tex);
            } else {
                // Convert on the decode thread: the device command context
                // must never be touched from the render thread, so the GPU→CPU
                // fallback happens here (ffmpeg CLI single-thread model).
                mf = TransferToSoftware(mf);
                if (!mf.IsValid()) {
                    SPDLOG_WARN("DecoderNode: hw frame download failed");
                    continue;
                }
            }
        }
        MaybeAnnounceFormat(mf.RawFrame());

        Timestamp ts;
        ts.pts = frame_pts;
        ts.time_base = {time_base_.num, time_base_.den};

        MediaBuffer buf(std::move(mf), ts);
        buf.set_serial(current_serial_);
        output_port_->Push(std::move(buf));
    }
}

void DecoderNode::MaybeAnnounceFormat(const AVFrame* frame) {
    // Audio frames use the same AVFrame::format field for sample formats;
    // this correction applies to video only.
    if (media_type_ != MediaType::kVideo || frame->format == announced_av_format_) {
        return;
    }
    announced_av_format_ = frame->format;

    PixelFormat pf = gpu::FromAvPixelFormat(frame->format);
    PixelFormat sw = PixelFormat::kUnknown;
    if (frame->hw_frames_ctx) {
        auto* fctx =
            reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        sw = fctx ? gpu::FromAvPixelFormat(fctx->sw_format)
                  : PixelFormat::kUnknown;
    }
    SPDLOG_INFO("DecoderNode: actual output format av={} domain={} sw={}",
                frame->format, static_cast<int>(pf), static_cast<int>(sw));
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
    // AVDISCARD_NONREF accelerates software catch-up by skipping non-ref
    // frames. Hardware decode must not use it: D3D11VA surfaces leak when
    // outputs are discarded, the pool drains and send_packet blocks forever
    // (mpv/ffplay likewise rely on PTS-drop only under hwaccel).
    if (codec_ctx_->hw_device_ctx == nullptr &&
        drop_until_pts_.load(std::memory_order_acquire) > 0.0) {
        codec_ctx_->skip_frame = AVDISCARD_NONREF;
    }
}

void DecoderNode::HandleEos() {
    // Drain remaining frames, then propagate EOS downstream.
    avcodec_send_packet(codec_ctx_, nullptr);
    DrainFrames();
    output_port_->Push(MediaBuffer::MakeEos(current_serial_));
}

void DecoderNode::ProcessPacket(MediaBuffer& buf) {
    if (!buf.IsPacket()) {
        return;  // Unexpected buffer type
    }
    AVPacketPtr& pkt = buf.AsPacket();
    if (!pkt.get() || !pkt->data) {
        // Null/empty packet — treat as a flush request.
        avcodec_send_packet(codec_ctx_, nullptr);
        DrainFrames();
        return;
    }
    int ret = avcodec_send_packet(codec_ctx_, pkt.get());
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        SPDLOG_WARN("DecoderNode: send_packet error {}", ret);
        return;
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
