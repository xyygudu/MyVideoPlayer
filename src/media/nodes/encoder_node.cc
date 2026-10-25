#include "nodes/encoder_node.h"

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#include <spdlog/spdlog.h>

#include "audio_resampler.h"
#include "ffmpeg_utils.h"
#include "graph/media_graph.h"

namespace mvp::graph {

namespace {

// The encoder's list of supported values for `config`; null = anything goes.
template <typename T>
const T* SupportedConfigs(const AVCodec* codec, AVCodecConfig config) {
    const void* configs = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, config, 0, &configs, nullptr) < 0) {
        return nullptr;
    }
    return static_cast<const T*>(configs);
}

AVPixelFormat ChoosePixelFormat(const AVCodec* codec) {
    const auto* fmts = SupportedConfigs<AVPixelFormat>(codec, AV_CODEC_CONFIG_PIX_FORMAT);
    return (fmts && fmts[0] != AV_PIX_FMT_NONE) ? fmts[0] : AV_PIX_FMT_YUV420P;
}

AVSampleFormat ChooseSampleFormat(const AVCodec* codec) {
    const auto* fmts = SupportedConfigs<AVSampleFormat>(codec, AV_CODEC_CONFIG_SAMPLE_FORMAT);
    return (fmts && fmts[0] != AV_SAMPLE_FMT_NONE) ? fmts[0] : AV_SAMPLE_FMT_FLTP;
}

// The source rate when the encoder takes it; otherwise the nearest higher
// supported rate (no band-limiting), else the highest one. Opus, for one,
// only accepts 48 kHz-family rates, so 44.1 kHz input becomes 48 kHz.
int ChooseSampleRate(const AVCodec* codec, int source_rate) {
    const int wanted = source_rate > 0 ? source_rate : 48000;
    const int* rates = SupportedConfigs<int>(codec, AV_CODEC_CONFIG_SAMPLE_RATE);
    if (!rates) {
        return wanted;
    }
    int above = 0;
    int below = 0;
    for (const int* r = rates; *r != 0; ++r) {
        if (*r == wanted) {
            return wanted;
        }
        if (*r > wanted && (above == 0 || *r < above)) {
            above = *r;
        }
        if (*r < wanted && *r > below) {
            below = *r;
        }
    }
    return above ? above : (below ? below : wanted);
}

// The default layout for the source channel count when supported; otherwise
// the supported layout with the most channels not above it, else the first.
void ChooseChannelLayout(const AVCodec* codec, int channels, AVChannelLayout* out) {
    AVChannelLayout wanted{};
    av_channel_layout_default(&wanted, channels > 0 ? channels : 2);
    const auto* layouts =
        SupportedConfigs<AVChannelLayout>(codec, AV_CODEC_CONFIG_CHANNEL_LAYOUT);
    const AVChannelLayout* pick = nullptr;
    for (const AVChannelLayout* l = layouts; l && l->nb_channels != 0; ++l) {
        if (av_channel_layout_compare(l, &wanted) == 0) {
            pick = l;
            break;
        }
        if (l->nb_channels <= wanted.nb_channels && (!pick || l->nb_channels > pick->nb_channels)) {
            pick = l;
        }
    }
    if (layouts && !pick) {
        pick = &layouts[0];
    }
    av_channel_layout_copy(out, pick ? pick : &wanted);
    av_channel_layout_uninit(&wanted);
}

}  // namespace

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
        // Audio pts count samples, so the time base follows the encoder rate.
        const int source_rate = fmt.AsAudio().sample_rate;
        sample_rate_ = ChooseSampleRate(codec_, source_rate);
        if (sample_rate_ != source_rate) {
            SPDLOG_INFO("{}: resampling audio {} Hz -> {} Hz (encoder constraint)", name_,
                        source_rate, sample_rate_);
        }
        time_base_ = AVRational{1, sample_rate_};
    }

    // Container decides where parameter sets go; read the downstream muxer's
    // requirement now so Prepare() can apply it before avcodec_open2.
    if (output_port_->Peer()) {
        global_header_ = output_port_->Peer()->Caps().header_placement ==
                         HeaderPlacement::kGlobal;
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
    if (codec_) {
        return true;  // Already resolved in DeclareCaps(); avoid a second lookup
    }
    codec_ = avcodec_find_encoder_by_name(params_.codec_name.c_str());
    if (!codec_) {
        SPDLOG_ERROR("EncoderNode: encoder '{}' not found", params_.codec_name);
        return false;
    }
    name_ = "EncoderNode(" + params_.codec_name + ")";
    return true;
}

void EncoderNode::DeclareCaps() {
    if (!FindEncoder()) {
        return;  // Negotiate() calls FindEncoder() again and reports the error
    }
    // Media type comes from the resolved encoder itself, not the (not yet
    // negotiated) input format — DeclareCaps runs before any node's Negotiate.
    FormatCaps caps;
    caps.media_type =
        (codec_->type == AVMEDIA_TYPE_AUDIO) ? MediaType::kAudio : MediaType::kVideo;
    caps.codec_ids = {codec_->id};
    output_port_->SetCaps(std::move(caps));
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
    if (global_header_) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary* opts = nullptr;
    ApplyRateControl(&opts);
    int ret = avcodec_open2(codec_ctx_, codec_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        SPDLOG_ERROR("EncoderNode: failed to open encoder '{}': {}", params_.codec_name,
                     AvErrorString(ret));
        avcodec_free_context(&codec_ctx_);
        return false;
    }
    if (media_type_ == MediaType::kAudio) {
        resampler_ = std::make_unique<AudioResampler>(
            codec_ctx_->sample_fmt, codec_ctx_->sample_rate, codec_ctx_->ch_layout);
    }
    return true;
}

void EncoderNode::ConfigureVideoContext(const VideoFormat& fmt) {
    codec_ctx_->width = fmt.width;
    codec_ctx_->height = fmt.height;
    codec_ctx_->pix_fmt = ChoosePixelFormat(codec_);
}

void EncoderNode::ConfigureAudioContext(const AudioFormat& fmt) {
    codec_ctx_->sample_rate = sample_rate_;
    codec_ctx_->sample_fmt = ChooseSampleFormat(codec_);
    ChooseChannelLayout(codec_, fmt.channels, &codec_ctx_->ch_layout);
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
    failed_ = false;
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
    resampler_.reset();
    if (audio_fifo_) {
        av_audio_fifo_free(audio_fifo_);
        audio_fifo_ = nullptr;
        audio_pts_valid_ = false;
    }
    last_video_pts_ = AV_NOPTS_VALUE;
    if (dropped_video_frames_ > 0) {
        SPDLOG_INFO("{}: {} frame(s) with non-increasing pts were dropped", name_, dropped_video_frames_);
        dropped_video_frames_ = 0;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
}

MediaFrame EncoderNode::ConvertVideoFrame(const MediaFrame& src) {
    const int dst_w = codec_ctx_->width;
    const int dst_h = codec_ctx_->height;
    if (!logged_rescale_ && (src.width() != dst_w || src.height() != dst_h)) {
        SPDLOG_WARN("{}: source frame is {}x{}, scaling to the encoder's {}x{}", name_,
                    src.width(), src.height(), dst_w, dst_h);
        logged_rescale_ = true;
    }
    // Rebuilt only when the source size/format changes mid-stream.
    sws_ctx_ = sws_getCachedContext(sws_ctx_, src.width(), src.height(),
                                    static_cast<AVPixelFormat>(src.format()), dst_w, dst_h,
                                    codec_ctx_->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        SPDLOG_ERROR("{}: no SwsContext for format {} -> {}", name_, src.format(),
                     static_cast<int>(codec_ctx_->pix_fmt));
        return MediaFrame();
    }

    MediaFrame dst = video_scratch_pool_.Acquire(dst_w, dst_h, codec_ctx_->pix_fmt);
    if (!dst.IsValid()) {
        SPDLOG_ERROR("{}: failed to allocate a conversion frame", name_);
        return MediaFrame();
    }
    const AVFrame* src_frame = src.RawFrame();
    AVFrame* dst_frame = dst.RawFrame();
    sws_scale(sws_ctx_, src_frame->data, src_frame->linesize, 0, src.height(),
              dst_frame->data, dst_frame->linesize);
    return dst;
}

bool EncoderNode::EnsureAudioFifo() {
    if (audio_fifo_) {
        return true;
    }
    int initial = codec_ctx_->frame_size > 0 ? codec_ctx_->frame_size * 2 : 4096;
    audio_fifo_ = av_audio_fifo_alloc(codec_ctx_->sample_fmt,
                                      codec_ctx_->ch_layout.nb_channels, initial);
    if (!audio_fifo_) {
        SPDLOG_ERROR("EncoderNode: failed to allocate audio fifo");
        return false;
    }
    return true;
}

void EncoderNode::ProcessAudioFrame(MediaFrame& mf, int64_t pts_ticks) {
    // The first buffered sample carries the timestamp of the frame it came
    // from; each full frame read out later advances by frame_size in time_base
    // units (audio time_base is 1/sample_rate, so PTS == sample index).
    if (!audio_pts_valid_) {
        audio_next_pts_ = pts_ticks;
        audio_pts_valid_ = true;
    }
    if (ResampleIntoFifo(mf.RawFrame())) {
        SendCompleteAudioFrames();
    }
}

bool EncoderNode::ResampleIntoFifo(const AVFrame* in) {
    if (!resampler_->Convert(in, resampled_.get())) {
        return false;  // Logged by the resampler; the frame is skipped.
    }
    const int samples = resampled_->nb_samples;
    if (samples == 0) {
        return true;  // Still buffered inside the resampler.
    }
    if (!EnsureAudioFifo()) {
        return false;
    }
    const int written = av_audio_fifo_write(
        audio_fifo_, reinterpret_cast<void**>(resampled_->extended_data), samples);
    if (written < samples) {
        SPDLOG_WARN("{}: audio fifo write short ({}/{})", name_, written, samples);
    }
    return true;
}

void EncoderNode::SendCompleteAudioFrames() {
    if (!audio_fifo_) {
        return;
    }
    int frame_size = codec_ctx_->frame_size;
    if (frame_size <= 0) {
        return;
    }
    while (av_audio_fifo_size(audio_fifo_) >= frame_size) {
        AVFramePtr frame;
        frame->format = codec_ctx_->sample_fmt;
        frame->sample_rate = codec_ctx_->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &codec_ctx_->ch_layout);
        frame->nb_samples = frame_size;
        if (av_frame_get_buffer(frame.get(), 0) < 0) {
            SPDLOG_ERROR("EncoderNode: failed to allocate audio frame buffer");
            return;
        }
        int read = av_audio_fifo_read(
            audio_fifo_, reinterpret_cast<void**>(frame->data), frame_size);
        if (read < frame_size) {
            SPDLOG_ERROR("EncoderNode: av_audio_fifo_read short ({}/{})", read,
                         frame_size);
            return;
        }
        frame->pts = audio_next_pts_;
        audio_next_pts_ += frame_size;
        if (!SendFrameAndDrain(frame.get())) {
            return;
        }
    }
}

void EncoderNode::FlushAudioFifo() {
    if (!audio_fifo_) {
        return;
    }
    int frame_size = codec_ctx_->frame_size;
    int remaining = av_audio_fifo_size(audio_fifo_);
    if (frame_size > 0 && remaining > 0) {
        // Pad the trailing partial frame with silence so the encoder gets a
        // complete frame_size frame (required by fixed-frame encoders like AAC).
        int pad = frame_size - remaining;
        AVFramePtr silence;
        silence->format = codec_ctx_->sample_fmt;
        silence->sample_rate = codec_ctx_->sample_rate;
        av_channel_layout_copy(&silence->ch_layout, &codec_ctx_->ch_layout);
        silence->nb_samples = pad;
        if (av_frame_get_buffer(silence.get(), 0) == 0) {
            av_samples_set_silence(silence->data, 0, pad,
                                   silence->ch_layout.nb_channels,
                                   static_cast<AVSampleFormat>(silence->format));
            av_audio_fifo_write(audio_fifo_,
                                reinterpret_cast<void**>(silence->data), pad);
        }
    }
    SendCompleteAudioFrames();
}

bool EncoderNode::SendFrameAndDrain(AVFrame* frame) {
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret == AVERROR(EAGAIN)) {
        // The encoder takes no input until its pending packets are drained.
        if (!DrainPackets()) {
            return false;
        }
        ret = avcodec_send_frame(codec_ctx_, frame);
    }
    if (ret < 0 && ret != AVERROR_EOF) {
        Fail("avcodec_send_frame failed: " + AvErrorString(ret));
        return false;
    }
    return DrainPackets();
}

void EncoderNode::ProcessFrame(MediaBuffer& buf) {
    if (!buf.IsFrame()) {
        return;
    }
    MediaFrame& mf = buf.AsFrame();
    if (!mf.IsValid()) {
        return;
    }
    int64_t pts_ticks =
        static_cast<int64_t>(buf.timestamp().pts / av_q2d(time_base_) + 0.5);

    if (media_type_ == MediaType::kVideo) {
        EncodeVideoFrame(mf, pts_ticks);
    } else if (media_type_ == MediaType::kAudio) {
        ProcessAudioFrame(mf, pts_ticks);
    }
}

void EncoderNode::EncodeVideoFrame(MediaFrame& mf, int64_t pts_ticks) {
    // Splices and timestamp glitches can repeat or reorder pts; like the
    // ffmpeg CLI's vfr frame sync, drop such frames instead of feeding the
    // encoder timestamps that turn into invalid DTS at the muxer.
    if (last_video_pts_ != AV_NOPTS_VALUE && pts_ticks <= last_video_pts_) {
        if (dropped_video_frames_++ == 0) {
            SPDLOG_WARN("{}: dropping frames whose pts does not increase (first at {})", name_,
                        pts_ticks);
        }
        return;
    }
    last_video_pts_ = pts_ticks;
    const bool needs_convert = mf.format() != static_cast<int>(codec_ctx_->pix_fmt) ||
                               mf.width() != codec_ctx_->width ||
                               mf.height() != codec_ctx_->height;
    MediaFrame converted;
    AVFrame* frame = mf.RawFrame();
    if (needs_convert) {
        converted = ConvertVideoFrame(mf);
        if (!converted.IsValid()) {
            return;
        }
        frame = converted.RawFrame();
    }
    frame->pts = pts_ticks;
    // Decoded frames still carry the source's picture types; encoders such as
    // libx264 would obey them and override gop_size/max_b_frames (ffmpeg's
    // encoder stage resets them too).
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    frame->flags &= ~AV_FRAME_FLAG_KEY;
    SendFrameAndDrain(frame);
}

void EncoderNode::HandleEos() {
    if (media_type_ == MediaType::kAudio) {
        ResampleIntoFifo(nullptr);  // Samples still held for rate conversion.
        FlushAudioFifo();
    }
    if (failed_ || !SendFrameAndDrain(nullptr)) {
        return;  // A failed or stopping encoder must not report completion.
    }
    output_port_->Push(MediaBuffer::MakeEos(current_serial_));
}

bool EncoderNode::DrainPackets() {
    while (running_.load(std::memory_order_relaxed)) {
        AVPacketPtr pkt;
        int ret = avcodec_receive_packet(codec_ctx_, pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            Fail("avcodec_receive_packet failed: " + AvErrorString(ret));
            return false;
        }

        Timestamp ts;
        ts.pts = pkt->pts * av_q2d(time_base_);
        ts.time_base = {time_base_.num, time_base_.den};

        BufferFlags flags = BufferFlags::kNone;
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            flags = flags | BufferFlags::kKeyFrame;
        }
        MediaBuffer buf(std::move(pkt), ts, flags);
        buf.set_serial(current_serial_);
        if (!output_port_->Push(std::move(buf))) {
            return false;  // Link aborted: the graph is stopping.
        }
    }
    return false;
}

void EncoderNode::Fail(const std::string& detail) {
    SPDLOG_ERROR("{}: {}", name_, detail);
    failed_ = true;
    if (graph_) {
        graph_->Bus().Post({GraphMessageType::kError, this, current_serial_, name_ + ": " + detail});
    }
}

void EncoderNode::EncodeLoop() {
    // A fatal encoder error ends the loop; the facade stops the graph when it
    // receives the posted kError.
    while (running_.load(std::memory_order_relaxed) && !failed_) {
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        current_serial_ = buf.serial();
        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            HandleEos();
            continue;
        }
        ProcessFrame(buf);
    }
}

}  // namespace mvp::graph
