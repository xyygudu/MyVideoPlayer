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

void Decoder::SetDropUntilPts(double pts) {
    drop_until_pts_.store(pts, std::memory_order_release);
}

void Decoder::DrainFrames(int serial) {
    while (running_) {
        AVFramePtr frame;
        int ret = avcodec_receive_frame(codec_ctx_, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        // 计算帧 pts（秒），用于 seek 快速丢帧判断
        double frame_pts = frame.get()->pts * av_q2d(stream_->time_base);
        double target = drop_until_pts_.load(std::memory_order_acquire);

        if (target > 0 && frame_pts < target) {
            // Seek 期间：目标帧之前的帧直接丢弃，不入队
            continue;
        }

        // 到达目标帧，恢复正常解码状态
        if (target > 0) {
            codec_ctx_->skip_frame = AVDISCARD_DEFAULT;
            drop_until_pts_.store(0, std::memory_order_release);
        }

        frame_queue_->Push(SerialFrame{std::move(frame), serial, false});
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

            // Seek 优化：跳过非参考帧解码以减少 GOP 内解码量
            if (drop_until_pts_.load(std::memory_order_acquire) > 0) {
                codec_ctx_->skip_frame = AVDISCARD_NONREF;
            }
        }

        // Discard stale packets pushed between flush-increment and actual seek
        if (pkt_serial != packet_queue_->serial()) {
            continue;
        }

        // Null packet (data==NULL) signals EOF from demuxer → enter drain mode
        if (!sp->pkt->data) {
            avcodec_send_packet(codec_ctx_, nullptr);
            DrainFrames(last_serial_);
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

        DrainFrames(pkt_serial);
    }
}

}  // namespace mvp
