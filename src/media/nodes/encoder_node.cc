#include "nodes/encoder_node.h"

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"

namespace mvp::graph {

EncoderNode::EncoderNode(mvp::EncodeParams params) : params_(std::move(params)) {
    input_port_ = std::make_unique<InputPort>(this);
    output_port_ = std::make_unique<OutputPort>(this);
}

EncoderNode::~EncoderNode() {
    Stop();
    CloseCodec();
}

bool EncoderNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("EncoderNode: input port not connected");
        return false;
    }
    const MediaFormat& fmt = input_port_->Format();
    if (!fmt.IsVideo() && !fmt.IsAudio()) {
        SPDLOG_ERROR("EncoderNode: input format is neither video nor audio");
        return false;
    }
    media_type_ = fmt.media_type();

    if (!FindEncoder()) {
        return false;
    }

    if (media_type_ == MediaType::kVideo) {
        Rational fr = fmt.AsVideo().frame_rate;
        time_base_ = (fr.num > 0) ? AVRational{fr.den, fr.num} : AVRational{1, 25};
    } else {
        int sr = fmt.AsAudio().sample_rate;
        time_base_ = AVRational{1, sr > 0 ? sr : 44100};
    }

    // Preliminary output format: codec_id + time_base only. Real codec
    // parameters (extradata) are only known after avcodec_open2 in
    // Prepare(), see PublishNegotiatedOutputFormat().
    output_port_->SetFormat(MediaFormat::FromStream(
        codec_->id, {time_base_.num, time_base_.den}, {0, 1}, nullptr,
        media_type_));
    return true;
}

bool EncoderNode::FindEncoder() {
    codec_ = avcodec_find_encoder_by_name(params_.codec_name.c_str());
    if (!codec_) {
        SPDLOG_ERROR("EncoderNode: encoder '{}' not found", params_.codec_name);
        return false;
    }
    name_ = "EncoderNode(" + params_.codec_name + ")";
    return true;
}

bool EncoderNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;  // Already prepared
    }
    if (state_ != NodeState::kConfigured && state_ != NodeState::kIdle) {
        SPDLOG_ERROR("EncoderNode: Prepare called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }
    if (!codec_) {
        SPDLOG_ERROR("EncoderNode: no encoder resolved from negotiation");
        state_ = NodeState::kError;
        return false;
    }

    if (!OpenCodec()) {
        state_ = NodeState::kError;
        return false;
    }

    PublishNegotiatedOutputFormat();
    state_ = NodeState::kPrepared;
    return true;
}

bool EncoderNode::OpenCodec() {
    codec_ctx_ = avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        SPDLOG_ERROR("EncoderNode: failed to allocate codec context");
        return false;
    }

    const MediaFormat& fmt = input_port_->Format();
    if (media_type_ == MediaType::kVideo) {
        ConfigureVideoContext(fmt.AsVideo());
    } else {
        ConfigureAudioContext(fmt.AsAudio());
    }
    codec_ctx_->time_base = time_base_;

    AVDictionary* opts = nullptr;
    ApplyRateControl(&opts);
    int ret = avcodec_open2(codec_ctx_, codec_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        SPDLOG_ERROR("EncoderNode: failed to open encoder '{}' (err {})",
                     params_.codec_name, ret);
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    return true;
}

void EncoderNode::ConfigureVideoContext(const VideoFormat& fmt) {
    codec_ctx_->width = fmt.width;
    codec_ctx_->height = fmt.height;
    codec_ctx_->pix_fmt =
        codec_->pix_fmts ? codec_->pix_fmts[0] : AV_PIX_FMT_YUV420P;
}

void EncoderNode::ConfigureAudioContext(const AudioFormat& fmt) {
    codec_ctx_->sample_rate = fmt.sample_rate;
    codec_ctx_->sample_fmt =
        codec_->sample_fmts ? codec_->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&codec_ctx_->ch_layout, fmt.channels);
}

void EncoderNode::ApplyRateControl(AVDictionary** opts) {
    if (media_type_ == MediaType::kAudio) {
        codec_ctx_->bit_rate = params_.bitrate_bps;
        return;
    }
    if (params_.rate_control == mvp::RateControlMode::kCrf) {
        av_dict_set_int(opts, "crf", params_.crf, 0);
    } else {
        codec_ctx_->bit_rate = params_.bitrate_bps;
    }
    if (!params_.preset.empty()) {
        av_dict_set(opts, "preset", params_.preset.c_str(), 0);
    }
    codec_ctx_->gop_size = params_.gop_size;
    codec_ctx_->max_b_frames = params_.max_b_frames;
}

void EncoderNode::PublishNegotiatedOutputFormat() {
    AVCodecParameters* av_params = avcodec_parameters_alloc();
    avcodec_parameters_from_context(av_params, codec_ctx_);
    output_port_->SetFormat(MediaFormat::FromStream(
        codec_ctx_->codec_id, {time_base_.num, time_base_.den}, {0, 1},
        av_params, media_type_));
    avcodec_parameters_free(&av_params);
}

bool EncoderNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("EncoderNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }

    running_ = true;
    encode_thread_ = std::thread(&EncoderNode::EncodeLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void EncoderNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }

    running_ = false;
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }

    CloseCodec();
    state_ = NodeState::kIdle;
}

void EncoderNode::Flush() {
    // Transcoder v1 has no seek support, so there is no cross-thread flush
    // to perform here. A future seek-capable facade would need the same
    // serial-based approach as DecoderNode::MaybeFlushOnSerialChange.
}

std::vector<InputPort*> EncoderNode::Inputs() {
    return {input_port_.get()};
}

std::vector<OutputPort*> EncoderNode::Outputs() {
    return {output_port_.get()};
}

void EncoderNode::CloseCodec() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
}

MediaFrame EncoderNode::ConvertVideoFrame(const MediaFrame& src) {
    int width = src.width();
    int height = src.height();
    int target_fmt = static_cast<int>(codec_ctx_->pix_fmt);

    if (!sws_ctx_) {
        sws_ctx_ = sws_getContext(width, height, static_cast<AVPixelFormat>(src.format()),
                                  width, height, static_cast<AVPixelFormat>(target_fmt),
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    if (!sws_ctx_) {
        SPDLOG_ERROR("EncoderNode: failed to create SwsContext for pixel conversion");
        return MediaFrame();
    }

    MediaFrame dst = video_scratch_pool_.Acquire(width, height, target_fmt, src.pts());
    AVFrame* src_frame = src.RawFrame();
    AVFrame* dst_frame = dst.RawFrame();
    sws_scale(sws_ctx_, src_frame->data, src_frame->linesize, 0, height,
             dst_frame->data, dst_frame->linesize);
    return dst;
}

AVFramePtr EncoderNode::ConvertAudioFrame(const MediaFrame& src) {
    AVFrame* src_frame = src.RawFrame();
    if (!swr_ctx_) {
        AVChannelLayout out_layout;
        av_channel_layout_copy(&out_layout, &codec_ctx_->ch_layout);
        int ret = swr_alloc_set_opts2(
            &swr_ctx_, &out_layout, codec_ctx_->sample_fmt, codec_ctx_->sample_rate,
            &src_frame->ch_layout, static_cast<AVSampleFormat>(src_frame->format),
            src_frame->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&out_layout);
        if (ret < 0 || !swr_ctx_ || swr_init(swr_ctx_) < 0) {
            SPDLOG_ERROR("EncoderNode: failed to init audio resampler");
            return AVFramePtr();
        }
    }

    AVFramePtr dst;
    dst->format = codec_ctx_->sample_fmt;
    dst->sample_rate = codec_ctx_->sample_rate;
    av_channel_layout_copy(&dst->ch_layout, &codec_ctx_->ch_layout);
    dst->nb_samples = src_frame->nb_samples;
    if (av_frame_get_buffer(dst.get(), 0) < 0) {
        SPDLOG_ERROR("EncoderNode: failed to allocate converted audio frame buffer");
        return AVFramePtr();
    }
    swr_convert(swr_ctx_, dst->data, dst->nb_samples,
               const_cast<const uint8_t**>(src_frame->data), src_frame->nb_samples);
    return dst;
}

void EncoderNode::SendFrameAndDrain(AVFrame* frame) {
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        SPDLOG_WARN("EncoderNode: send_frame error {}", ret);
        return;
    }
    DrainPackets();
}

void EncoderNode::ProcessFrame(MediaBuffer& buf) {
    if (!buf.IsFrame()) {
        return;
    }
    MediaFrame& mf = buf.AsFrame();
    if (!mf.IsValid()) {
        return;
    }
    int64_t pts_ticks = static_cast<int64_t>(mf.pts() / av_q2d(time_base_) + 0.5);

    if (media_type_ == MediaType::kVideo) {
        bool needs_convert = mf.format() != static_cast<int>(codec_ctx_->pix_fmt);
        if (!needs_convert) {
            mf.RawFrame()->pts = pts_ticks;
            SendFrameAndDrain(mf.RawFrame());
            return;
        }
        MediaFrame converted = ConvertVideoFrame(mf);
        if (!converted.IsValid()) return;
        converted.RawFrame()->pts = pts_ticks;
        SendFrameAndDrain(converted.RawFrame());
    } else if (media_type_ == MediaType::kAudio) {
        AVFramePtr converted = ConvertAudioFrame(mf);
        if (!converted->data[0]) return;
        converted->pts = pts_ticks;
        SendFrameAndDrain(converted.get());
    }
}

void EncoderNode::HandleEos() {
    avcodec_send_frame(codec_ctx_, nullptr);
    DrainPackets();
    output_port_->Push(MediaBuffer::MakeEos(media_type_));
}

void EncoderNode::DrainPackets() {
    while (running_.load(std::memory_order_relaxed)) {
        AVPacketPtr pkt;
        int ret = avcodec_receive_packet(codec_ctx_, pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            SPDLOG_WARN("EncoderNode: receive_packet error {}", ret);
            break;
        }

        Timestamp ts;
        ts.pts = pkt->pts * av_q2d(time_base_);
        ts.dts = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts * av_q2d(time_base_) : ts.pts;
        ts.duration = pkt->duration * av_q2d(time_base_);
        ts.time_base = {time_base_.num, time_base_.den};

        BufferFlags flags = BufferFlags::kNone;
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            flags = flags | BufferFlags::kKeyFrame;
        }
        MediaBuffer buf(std::move(pkt), media_type_, ts, flags);
        output_port_->Push(std::move(buf));
    }
}

void EncoderNode::EncodeLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            HandleEos();
            continue;
        }
        ProcessFrame(buf);
    }
}

}  // namespace mvp::graph
