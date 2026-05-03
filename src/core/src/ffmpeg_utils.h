#ifndef MVP_FFMPEG_UTILS_H_
#define MVP_FFMPEG_UTILS_H_

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
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

}  // namespace mvp

#endif  // MVP_FFMPEG_UTILS_H_
