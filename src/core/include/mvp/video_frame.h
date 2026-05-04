#ifndef MVP_VIDEO_FRAME_H_
#define MVP_VIDEO_FRAME_H_

#include <cstdint>
#include <memory>

#include "mvp/export.h"

namespace mvp {

enum class PixelFormat {
    kUnknown = 0,
    kYUV420P,
    kYUV422P,
    kYUV444P,
    kNV12,
    kRGB32,
};

class MVP_CORE_EXPORT VideoFrame {
  public:
    VideoFrame();
    ~VideoFrame();

    VideoFrame(VideoFrame&& other) noexcept;
    VideoFrame& operator=(VideoFrame&& other) noexcept;

    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;

    // Accessors
    const uint8_t* data(int plane) const;
    int linesize(int plane) const;
    int width() const;
    int height() const;
    PixelFormat format() const;
    double pts() const;

    // Check if this frame holds valid data
    bool IsValid() const;

  private:
    friend class FrameConverter;
    friend class VideoRenderer;
    template <typename>
    friend struct StreamContext;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_VIDEO_FRAME_H_
