#ifndef MVP_MEDIA_FRAME_H_
#define MVP_MEDIA_FRAME_H_

#include "ffmpeg_utils.h"

struct AVFrame;

namespace mvp {

/// Media type tag for distinguishing stream types in the pipeline.
enum class MediaType {
    kUnknown = 0,
    kAudio,
    kVideo,
    kSubtitle,  // Reserved for future use
};

/// Internal unified frame type for pipeline transport.
/// Wraps an AVFrame with PTS and MediaType tag. Move-only.
/// Used between Decoder → FrameQueue → Renderer boundary.
/// NOT exposed in public API — VideoFrame/AudioFrame remain the public types.
class MediaFrame {
  public:
    MediaFrame();
    MediaFrame(AVFrame* src, double pts, MediaType type);
    ~MediaFrame();

    MediaFrame(MediaFrame&& other) noexcept;
    MediaFrame& operator=(MediaFrame&& other) noexcept;

    MediaFrame(const MediaFrame&) = delete;
    MediaFrame& operator=(const MediaFrame&) = delete;

    double pts() const;
    bool IsValid() const;
    MediaType type() const;

    /// Access the underlying AVFrame. Internal use only.
    AVFrame* RawFrame() const;

  private:
    AVFramePtr frame_;
    double pts_{0.0};
    MediaType type_{MediaType::kUnknown};
};

}  // namespace mvp

#endif  // MVP_MEDIA_FRAME_H_
