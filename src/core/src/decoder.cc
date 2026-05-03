#include "decoder.h"

#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "frame_queue.h"
#include "packet_queue.h"

namespace mvp {

Decoder::Decoder()
    : codec_ctx_(nullptr),
      stream_(nullptr),
      packet_queue_(nullptr),
      frame_queue_(nullptr),
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
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    stream_ = nullptr;
}

void Decoder::Start(PacketQueue* packet_queue, FrameQueue* frame_queue) {
    if (running_) return;
    packet_queue_ = packet_queue;
    frame_queue_ = frame_queue;

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
    while (running_) {
        auto sp = packet_queue_->Pop();
        if (!sp) {
            break;  // Aborted
        }

        int pkt_serial = sp->serial;

        // Serial changed → seek happened, flush codec internal buffers
        if (pkt_serial != last_serial_) {
            avcodec_flush_buffers(codec_ctx_);
            last_serial_ = pkt_serial;
        }

        // Discard stale packets pushed between flush-increment and actual seek
        if (pkt_serial != packet_queue_->serial()) {
            continue;
        }

        // Null packet (data==NULL) signals EOF from demuxer → enter drain mode
        if (!sp->pkt->data) {
            avcodec_send_packet(codec_ctx_, nullptr);

            while (running_) {
                AVFramePtr frame;
                int drain_ret = avcodec_receive_frame(codec_ctx_, frame.get());
                if (drain_ret == AVERROR_EOF || drain_ret == AVERROR(EAGAIN)) break;
                if (drain_ret < 0) break;
                frame_queue_->Push(SerialFrame{std::move(frame), last_serial_, false});
            }

            frame_queue_->PushEof(last_serial_);

            // Wait for either abort or a new serial (indicating seek)
            while (running_) {
                int current_serial = packet_queue_->serial();
                if (current_serial != last_serial_) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        int ret = avcodec_send_packet(codec_ctx_, sp->pkt.get());
        if (ret < 0) continue;

        while (ret >= 0 && running_) {
            AVFramePtr frame;
            ret = avcodec_receive_frame(codec_ctx_, frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            frame_queue_->Push(SerialFrame{std::move(frame), pkt_serial, false});
        }
    }
}

}  // namespace mvp
