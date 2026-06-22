#include "nodes/decoder_node.h"

#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "graph/media_graph.h"
#include "hw_accel_context.h"
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
    // Try to read codec parameters from the input port (new path).
    // If input port has format with codec_params, cache for Prepare().
    if (input_port_->IsConnected()) {
        const MediaFormat& fmt = input_port_->Format();
        if (fmt.codec_params()) {
            negotiated_codecpar_ = fmt.codec_params();
            time_base_ = {fmt.time_base().num, fmt.time_base().den};

            // Determine media type from codec params
            if (fmt.media_type() == MediaType::kVideo) {
                media_type_ = MediaType::kVideo;
                name_ = "DecoderNode(video)";
            } else if (fmt.media_type() == MediaType::kAudio) {
                media_type_ = MediaType::kAudio;
                name_ = "DecoderNode(audio)";
            }
        }
    }
    return true;
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

    // Determine codec parameters source: from port negotiation
    const AVCodecParameters* codecpar = negotiated_codecpar_;
    if (!codecpar) {
        SPDLOG_ERROR("DecoderNode: no codec params from negotiation");
        state_ = NodeState::kError;
        return false;
    }

    // Find codec
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        SPDLOG_ERROR("DecoderNode: codec not found for id {}",
                     static_cast<int>(codecpar->codec_id));
        state_ = NodeState::kError;
        return false;
    }

    // Allocate context
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        state_ = NodeState::kError;
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, codecpar) < 0) {
        SPDLOG_ERROR("DecoderNode: failed to copy codec params");
        avcodec_free_context(&codec_ctx_);
        state_ = NodeState::kError;
        return false;
    }

    // Hardware acceleration: query from graph shared resource
    if (graph_ && graph_->HWDevice() && media_type_ == MediaType::kVideo) {
        auto* hw = graph_->HWDevice().get();
        if (hw->DeviceRef()) {
            codec_ctx_->opaque = hw;
            codec_ctx_->get_format = mvp::HWAccelContext::GetFormat;
            codec_ctx_->hw_device_ctx = av_buffer_ref(hw->DeviceRef());
        }
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        SPDLOG_ERROR("DecoderNode: failed to open codec '{}'", codec->name);
        avcodec_free_context(&codec_ctx_);
        state_ = NodeState::kError;
        return false;
    }

    // Set output port format
    if (media_type_ == MediaType::kVideo) {
        // Frame rate comes from the negotiated input format (originally from DemuxNode)
        const auto& in_fmt = input_port_->Format();
        Rational fr = in_fmt.frame_rate();
        output_port_->SetFormat(MediaFormat::Video(
            codec_ctx_->width, codec_ctx_->height,
            PixelFormat::kYUV420P,  // Will be refined at runtime
            fr));
    } else if (media_type_ == MediaType::kAudio) {
        output_port_->SetFormat(MediaFormat::Audio(
            codec_ctx_->sample_rate, codec_ctx_->ch_layout.nb_channels,
            SampleFormat::kFloat));  // Placeholder, refined at runtime
    }

    bool has_hw = (graph_ && graph_->HWDevice() && media_type_ == MediaType::kVideo);
    SPDLOG_INFO("DecoderNode: opened codec '{}' ({}){}",
                codec->name, (media_type_ == MediaType::kVideo ? "video" : "audio"),
                has_hw ? " [HW accel]" : "");

    state_ = NodeState::kPrepared;
    return true;
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
    drop_until_pts_.store(pts, std::memory_order_release);
    if (codec_ctx_ && pts > 0) {
        codec_ctx_->skip_frame = AVDISCARD_NONREF;
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

        MediaFrame mf(frame.get(), frame_pts, media_type_);
        Timestamp ts;
        ts.pts = frame_pts;
        ts.duration = frame->duration * av_q2d(time_base_);
        ts.time_base = {time_base_.num, time_base_.den};

        MediaBuffer buf(std::move(mf), ts);
        output_port_->Push(std::move(buf));
    }
}

void DecoderNode::DecodeLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        // Pull packet from input link
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }

        MediaBuffer& buf = *opt_buf;

        // --- Serial-based flush detection ---
        // After a seek, Link::Flush() increments the serial. New packets
        // carry the new serial. When we detect a serial change, flush the
        // codec on THIS thread (safe) to clear stale reference frames.
        int buf_serial = buf.serial();
        if (buf_serial != last_serial_) {
            avcodec_flush_buffers(codec_ctx_);
            last_serial_ = buf_serial;

            // Re-apply skip_frame if drop target is active
            if (drop_until_pts_.load(std::memory_order_acquire) > 0.0) {
                codec_ctx_->skip_frame = AVDISCARD_NONREF;
            }
        }

        // EOS → drain codec and propagate
        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            avcodec_send_packet(codec_ctx_, nullptr);
            DrainFrames();
            output_port_->Push(MediaBuffer::MakeEos(media_type_));

            // Wait for either stop or new data (after a seek)
            while (running_.load(std::memory_order_relaxed)) {
                auto next = input_port_->Pull();
                if (!next) break;
                // Got new data — update serial tracking and process
                buf = std::move(*next);
                buf_serial = buf.serial();
                if (buf_serial != last_serial_) {
                    avcodec_flush_buffers(codec_ctx_);
                    last_serial_ = buf_serial;
                    if (drop_until_pts_.load(std::memory_order_acquire) > 0.0) {
                        codec_ctx_->skip_frame = AVDISCARD_NONREF;
                    }
                }
                goto process_packet;
            }
            break;
        }

    process_packet:
        if (!buf.IsPacket()) {
            continue;  // Unexpected buffer type
        }

        AVPacketPtr& pkt = buf.AsPacket();
        if (!pkt.get() || !pkt->data) {
            // Null/empty packet — treat as EOS signal
            avcodec_send_packet(codec_ctx_, nullptr);
            DrainFrames();
            continue;
        }

        int ret = avcodec_send_packet(codec_ctx_, pkt.get());
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            SPDLOG_WARN("DecoderNode: send_packet error {}", ret);
            continue;
        }

        DrainFrames();
    }
}

}  // namespace mvp::graph
