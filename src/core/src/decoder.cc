#include "decoder.h"

#include "frame_queue.h"
#include "packet_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace mvp {

Decoder::Decoder()
    : codec_ctx_(nullptr),
      stream_(nullptr),
      packet_queue_(nullptr),
      frame_queue_(nullptr),
      sws_ctx_(nullptr),
      convert_to_rgb_(false),
      dst_width_(0),
      dst_height_(0),
      running_(false) {}

Decoder::~Decoder() { Close(); }

bool Decoder::Open(AVStream* stream) {
    Close();
    stream_ = stream;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        SPDLOG_ERROR("Decoder: codec not found for id {}", (int)stream->codecpar->codec_id);
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) return false;

    if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0) {
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        SPDLOG_ERROR("Decoder: failed to open codec '{}'", codec->name);
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    SPDLOG_INFO("Decoder: opened codec '{}'", codec->name);
    return true;
}

void Decoder::Close() {
    Stop();
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    stream_ = nullptr;
}

void Decoder::Start(PacketQueue* packet_queue, FrameQueue* frame_queue, bool convert_to_rgb) {
    if (running_) return;
    packet_queue_ = packet_queue;
    frame_queue_ = frame_queue;
    convert_to_rgb_ = convert_to_rgb;

    if (convert_to_rgb_ && codec_ctx_) {
        dst_width_ = codec_ctx_->width;
        dst_height_ = codec_ctx_->height;
        sws_ctx_ =
            sws_getContext(dst_width_, dst_height_, codec_ctx_->pix_fmt, dst_width_, dst_height_,
                           AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    running_ = true;
    decode_thread_ = std::thread(&Decoder::DecodeLoop, this);
}

void Decoder::Stop() {
    running_ = false;
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
}

void Decoder::DecodeLoop() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = nullptr;
    uint8_t* rgb_buffer = nullptr;

    if (convert_to_rgb_ && sws_ctx_) {
        rgb_frame = av_frame_alloc();
        int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32, dst_width_, dst_height_, 1);
        rgb_buffer = static_cast<uint8_t*>(av_malloc(buffer_size));
        av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer, AV_PIX_FMT_RGB32,
                             dst_width_, dst_height_, 1);
        rgb_frame->width = dst_width_;
        rgb_frame->height = dst_height_;
        rgb_frame->format = AV_PIX_FMT_RGB32;
    }

    while (running_) {
        int pkt_serial = 0;
        if (!packet_queue_->Pop(pkt, &pkt_serial)) {
            break;  // Aborted
        }

        // Serial changed → seek happened, flush codec internal buffers
        if (pkt_serial != last_serial_) {
            avcodec_flush_buffers(codec_ctx_);
            last_serial_ = pkt_serial;
        }

        // Discard stale packets pushed between flush-increment and actual seek
        if (pkt_serial != packet_queue_->serial()) {
            av_packet_unref(pkt);
            continue;
        }

        int ret = avcodec_send_packet(codec_ctx_, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (ret >= 0 && running_) {
            ret = avcodec_receive_frame(codec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            if (convert_to_rgb_ && sws_ctx_ && rgb_frame) {
                sws_scale(sws_ctx_, frame->data, frame->linesize, 0, frame->height,
                          rgb_frame->data, rgb_frame->linesize);
                rgb_frame->pts = frame->pts;

                AVFrame* out = av_frame_alloc();
                av_frame_ref(out, rgb_frame);
                out->pts = frame->pts;
                frame_queue_->Push(out, last_serial_);
                av_frame_free(&out);
            } else {
                frame_queue_->Push(frame, last_serial_);
            }
            av_frame_unref(frame);
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    if (rgb_frame) av_frame_free(&rgb_frame);
    if (rgb_buffer) av_free(rgb_buffer);
}

}  // namespace mvp
