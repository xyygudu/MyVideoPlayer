#ifndef MVP_FFMPEG_UTILS_H_
#define MVP_FFMPEG_UTILS_H_

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace mvp {

// RAII wrapper for AVFrame.
// Move-only: AVFrame* has unique ownership (like unique_ptr).
// Copying would cause double av_frame_free; use av_frame_ref for data sharing.
class AVFramePtr {
  public:
    AVFramePtr() : frame_(av_frame_alloc()) {}
    ~AVFramePtr() { av_frame_free(&frame_); }

    AVFramePtr(AVFramePtr&& other) noexcept : frame_(other.frame_) {
        other.frame_ = nullptr;
    }

    AVFramePtr& operator=(AVFramePtr&& other) noexcept {
        if (this != &other) {
            av_frame_free(&frame_);
            frame_ = other.frame_;
            other.frame_ = nullptr;
        }
        return *this;
    }

    AVFramePtr(const AVFramePtr&) = delete;
    AVFramePtr& operator=(const AVFramePtr&) = delete;

    AVFrame* get() const { return frame_; }
    AVFrame* operator->() const { return frame_; }
    explicit operator bool() const { return frame_ != nullptr; }

    void unref() { av_frame_unref(frame_); }

  private:
    AVFrame* frame_;
};

// RAII wrapper for AVPacket.
// Move-only: same rationale as AVFramePtr — unique ownership of the shell.
class AVPacketPtr {
  public:
    AVPacketPtr() : pkt_(av_packet_alloc()) {}
    ~AVPacketPtr() { av_packet_free(&pkt_); }

    AVPacketPtr(AVPacketPtr&& other) noexcept : pkt_(other.pkt_) {
        other.pkt_ = nullptr;
    }

    AVPacketPtr& operator=(AVPacketPtr&& other) noexcept {
        if (this != &other) {
            av_packet_free(&pkt_);
            pkt_ = other.pkt_;
            other.pkt_ = nullptr;
        }
        return *this;
    }

    AVPacketPtr(const AVPacketPtr&) = delete;
    AVPacketPtr& operator=(const AVPacketPtr&) = delete;

    AVPacket* get() const { return pkt_; }
    AVPacket* operator->() const { return pkt_; }
    explicit operator bool() const { return pkt_ != nullptr; }

    void unref() { av_packet_unref(pkt_); }

  private:
    AVPacket* pkt_;
};

// Whether `av_pix_fmt` is one of the planar/semi-planar YUV formats the
// hand-written effect nodes (TransformEffectNode, ColorEffectNode) know how
// to process. Other formats (e.g. RGB32) are passed through unmodified by
// those nodes rather than mis-interpreted as YUV.
inline bool IsPlanarYuvPixelFormat(int av_pix_fmt) {
    switch (av_pix_fmt) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_NV12:
            return true;
        default:
            return false;
    }
}

// Layout of a YUV chroma plane, in the units the effect nodes operate on.
//
// `interleaved` is true only for NV12, where a single plane (data[1]) holds
// alternating U/V bytes; planar formats (420/422/444) instead use two
// independent planes (data[1]=U, data[2]=V) and this struct describes the
// (identical) layout of either one.
//
// `width`/`height` are the plane's own dimensions: for interleaved formats,
// `width` counts *component pairs* (i.e. matches the U-only/V-only sample
// count per row, not raw bytes) so callers can address individual U/V
// samples with a component stride of 2, offset 0 (U) or 1 (V).
struct ChromaPlaneLayout {
    bool interleaved{false};
    int width{0};
    int height{0};
};

// Returns {false, 0, 0} for formats IsPlanarYuvPixelFormat() rejects —
// callers should treat that as "no chroma plane to process".
inline ChromaPlaneLayout ComputeChromaPlaneLayout(int av_pix_fmt, int luma_width,
                                                  int luma_height) {
    switch (av_pix_fmt) {
        case AV_PIX_FMT_YUV420P:
            return {false, (luma_width + 1) / 2, (luma_height + 1) / 2};
        case AV_PIX_FMT_YUV422P:
            return {false, (luma_width + 1) / 2, luma_height};
        case AV_PIX_FMT_YUV444P:
            return {false, luma_width, luma_height};
        case AV_PIX_FMT_NV12:
            return {true, (luma_width + 1) / 2, (luma_height + 1) / 2};
        default:
            return {false, 0, 0};
    }
}

}  // namespace mvp

#endif  // MVP_FFMPEG_UTILS_H_
