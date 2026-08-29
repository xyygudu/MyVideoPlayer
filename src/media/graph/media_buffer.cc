#include "graph/media_buffer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

namespace {
// --- Debug frame dump helpers (see MediaBuffer::SaveFrame) ---------------

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool IsImageExtension(const std::string& lower) {
    return EndsWith(lower, ".png") || EndsWith(lower, ".bmp") ||
           EndsWith(lower, ".jpg") || EndsWith(lower, ".jpeg") ||
           EndsWith(lower, ".ppm") || EndsWith(lower, ".pnm");
}

// Convert any software frame to tightly-packed RGB24 and write a P6 PPM.
bool WritePpm(const AVFrame* frame, const std::string& path) {
    const int width = frame->width;
    const int height = frame->height;
    SwsContext* sws = sws_getContext(width, height,
                                     static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: sws_getContext failed for {}x{}",
                     width, height);
        return false;
    }

    const int size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
    if (size < 0) {
        sws_freeContext(sws);
        SPDLOG_ERROR("MediaBuffer::SaveFrame: RGB24 buffer size failed");
        return false;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(size));
    uint8_t* dst_data[4] = {nullptr};
    int dst_linesize[4] = {0};
    if (av_image_fill_arrays(dst_data, dst_linesize, rgb.data(), AV_PIX_FMT_RGB24,
                             width, height, 1) < 0) {
        sws_freeContext(sws);
        SPDLOG_ERROR("MediaBuffer::SaveFrame: RGB24 fill_arrays failed");
        return false;
    }

    const int ret = sws_scale(sws, frame->data, frame->linesize, 0, height,
                              dst_data, dst_linesize);
    sws_freeContext(sws);
    if (ret < 0) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: sws_scale failed");
        return false;
    }

    // av_image_fill_arrays(align=1) packs rows tightly, so rgb is a valid body.
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: cannot open {}", path);
        return false;
    }
    const std::string header = "P6\n" + std::to_string(width) + " " +
                               std::to_string(height) + "\n255\n";
    ofs.write(header.data(), static_cast<std::streamsize>(header.size()));
    ofs.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
    return ofs.good();
}

// Write the frame's planes tightly packed, preserving the pixel layout.
bool WriteRawPlanes(const AVFrame* frame, const std::string& path) {
    const auto fmt = static_cast<AVPixelFormat>(frame->format);
    const int size = av_image_get_buffer_size(fmt, frame->width, frame->height, 1);
    if (size < 0) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: unsupported/unknown pixel format {}",
                     frame->format);
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    const int ret = av_image_copy_to_buffer(buf.data(), size, frame->data,
                                            frame->linesize, fmt,
                                            frame->width, frame->height, 1);
    if (ret < 0) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: av_image_copy_to_buffer failed");
        return false;
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: cannot open {}", path);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    return ofs.good();
}

// Write raw PCM samples for audio frames (planar or interleaved).
bool WriteRawAudio(const AVFrame* frame, const std::string& path) {
    if (frame->nb_samples <= 0 || frame->ch_layout.nb_channels <= 0) {
        SPDLOG_WARN("MediaBuffer::SaveFrame: frame has no audio samples");
        return false;
    }
    const auto fmt = static_cast<AVSampleFormat>(frame->format);
    const int bytes = av_get_bytes_per_sample(fmt);
    if (bytes <= 0) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: unsupported sample format {}", frame->format);
        return false;
    }
    const bool planar = av_sample_fmt_is_planar(fmt);
    const int nb_ch = frame->ch_layout.nb_channels;
    const int plane_bytes = frame->nb_samples * bytes * (planar ? 1 : nb_ch);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: cannot open {}", path);
        return false;
    }
    if (planar) {
        for (int c = 0; c < nb_ch; ++c) {
            const uint8_t* p = frame->extended_data[c];
            ofs.write(reinterpret_cast<const char*>(p), plane_bytes);
        }
    } else {
        ofs.write(reinterpret_cast<const char*>(frame->extended_data[0]),
                  plane_bytes);
    }
    return ofs.good();
}

// Scale a software frame into `dst` allocated in `target` pixel format.
bool ScaleToFormat(const AVFrame* src, AVFrame* dst, AVPixelFormat target) {
    dst->format = target;
    dst->width = src->width;
    dst->height = src->height;
    if (av_frame_get_buffer(dst, 0) < 0) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: av_frame_get_buffer failed");
        return false;
    }
    SwsContext* sws = sws_getContext(src->width, src->height,
                                     static_cast<AVPixelFormat>(src->format),
                                     src->width, src->height, target,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: sws_getContext failed");
        return false;
    }
    const int ret = sws_scale(sws, src->data, src->linesize, 0, src->height,
                              dst->data, dst->linesize);
    sws_freeContext(sws);
    return ret >= 0;
}

// Encode one software frame to a single-image file using an FFmpeg encoder.
bool EncodeFrameToBuffer(const AVFrame* src, AVCodecID codec_id,
                         AVPixelFormat target, std::vector<uint8_t>& out) {
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: no encoder for codec {}",
                     static_cast<int>(codec_id));
        return false;
    }

    mvp::AVFramePtr converted;
    if (!ScaleToFormat(src, converted.get(), target)) {
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: avcodec_alloc_context3 failed");
        return false;
    }
    ctx->width = src->width;
    ctx->height = src->height;
    ctx->pix_fmt = target;
    ctx->time_base.num = 1;
    ctx->time_base.den = 25;
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        SPDLOG_ERROR("MediaBuffer::SaveFrame: avcodec_open2 failed");
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&ctx);
        SPDLOG_ERROR("MediaBuffer::SaveFrame: av_packet_alloc failed");
        return false;
    }

    auto drain = [&]() {
        while (avcodec_receive_packet(ctx, pkt) >= 0) {
            out.insert(out.end(), pkt->data, pkt->data + pkt->size);
            av_packet_unref(pkt);
        }
    };
    // Send the frame, then flush so the encoder emits any trailing packet.
    if (avcodec_send_frame(ctx, converted.get()) >= 0) {
        drain();
        avcodec_send_frame(ctx, nullptr);
        drain();
    }
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return !out.empty();
}

bool WriteBytesToFile(const std::vector<uint8_t>& data, const std::string& path) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: cannot open {}", path);
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

bool EncodeImageFile(const AVFrame* src, const std::string& path,
                     AVCodecID codec_id, AVPixelFormat target) {
    std::vector<uint8_t> data;
    if (!EncodeFrameToBuffer(src, codec_id, target, data)) {
        SPDLOG_ERROR("MediaBuffer::SaveFrame: encoder produced no data");
        return false;
    }
    return WriteBytesToFile(data, path);
}

// Dispatch an image save by file extension.
bool SaveImage(const AVFrame* frame, const std::string& path,
               const std::string& lower) {
    if (frame->width <= 0 || frame->height <= 0) {
        SPDLOG_WARN("MediaBuffer::SaveFrame: not a video frame ({}x{})",
                    frame->width, frame->height);
        return false;
    }
    if (EndsWith(lower, ".ppm") || EndsWith(lower, ".pnm")) {
        return WritePpm(frame, path);
    }
    if (EndsWith(lower, ".png")) {
        return EncodeImageFile(frame, path, AV_CODEC_ID_PNG, AV_PIX_FMT_RGB24);
    }
    if (EndsWith(lower, ".bmp")) {
        return EncodeImageFile(frame, path, AV_CODEC_ID_BMP, AV_PIX_FMT_BGR24);
    }
    if (EndsWith(lower, ".jpg") || EndsWith(lower, ".jpeg")) {
        return EncodeImageFile(frame, path, AV_CODEC_ID_MJPEG, AV_PIX_FMT_YUV420P);
    }
    SPDLOG_WARN("MediaBuffer::SaveFrame: unsupported image extension");
    return false;
}
}  // namespace

namespace mvp::graph {

MediaBuffer::MediaBuffer(AVPacketPtr pkt, Timestamp ts, BufferFlags flags)
    : payload_(std::move(pkt)), timestamp_(ts), flags_(flags) {}

MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)), timestamp_(ts), flags_(flags) {}

MediaBuffer MediaBuffer::MakeEos(int serial) {
    MediaBuffer buf;
    buf.flags_ = BufferFlags::kEos;
    buf.serial_ = serial;
    return buf;
}

MediaBuffer::~MediaBuffer() = default;

MediaBuffer::MediaBuffer(MediaBuffer&& other) noexcept
    : payload_(std::move(other.payload_)),
      timestamp_(other.timestamp_),
      flags_(other.flags_),
      serial_(other.serial_) {
    other.flags_ = BufferFlags::kNone;
    other.serial_ = 0;
}

MediaBuffer& MediaBuffer::operator=(MediaBuffer&& other) noexcept {
    if (this != &other) {
        payload_ = std::move(other.payload_);
        timestamp_ = other.timestamp_;
        flags_ = other.flags_;
        serial_ = other.serial_;
        other.flags_ = BufferFlags::kNone;
        other.serial_ = 0;
    }
    return *this;
}

bool MediaBuffer::IsPacket() const {
    return std::holds_alternative<AVPacketPtr>(payload_);
}

bool MediaBuffer::IsFrame() const {
    return std::holds_alternative<MediaFrame>(payload_);
}

bool MediaBuffer::IsValid() const {
    if (IsPacket()) {
        return std::get<AVPacketPtr>(payload_).get() != nullptr;
    }
    if (IsFrame()) {
        return std::get<MediaFrame>(payload_).IsValid();
    }
    // EOS-only buffers are valid if they have the EOS flag.
    return HasFlag(flags_, BufferFlags::kEos);
}

AVPacketPtr& MediaBuffer::AsPacket() {
    return std::get<AVPacketPtr>(payload_);
}

const AVPacketPtr& MediaBuffer::AsPacket() const {
    return std::get<AVPacketPtr>(payload_);
}

MediaFrame& MediaBuffer::AsFrame() {
    return std::get<MediaFrame>(payload_);
}

const MediaFrame& MediaBuffer::AsFrame() const {
    return std::get<MediaFrame>(payload_);
}

bool MediaBuffer::SaveFrame(const std::string& path) const {
    if (!IsFrame()) {
        SPDLOG_WARN("MediaBuffer::SaveFrame: payload is not a frame");
        return false;
    }
    const MediaFrame& frame = AsFrame();

    // Hardware frames can't be read directly (data[0] is a device texture);
    // copy into system memory first.
    MediaFrame sw;
    if (frame.IsHardware()) {
        sw = mvp::TransferToSoftware(frame);
        if (!sw.IsValid()) {
            SPDLOG_ERROR("MediaBuffer::SaveFrame: GPU->CPU transfer failed");
            return false;
        }
    }
    const MediaFrame& target = sw.IsValid() ? sw : frame;

    const std::string lower = ToLower(path);
    if (IsImageExtension(lower)) {
        return SaveImage(target.RawFrame(), path, lower);
    }
    // Non-image dump: video planes, or PCM for audio frames.
    const AVFrame* raw = target.RawFrame();
    if (raw->width > 0 && raw->height > 0) {
        return WriteRawPlanes(raw, path);
    }
    return WriteRawAudio(raw, path);
}

}  // namespace mvp::graph
