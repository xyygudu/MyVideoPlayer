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

/// Pixel format tag for decoded video frames.
enum class PixelFormat {
    kUnknown = 0,
    kYUV420P,
    kYUV422P,
    kYUV444P,
    kNV12,
    kRGB32,
    kD3D11,  // Hardware frame (D3D11VA output), data[0] is ID3D11Texture2D*
};

/// Sample format tag for decoded audio frames.
enum class SampleFormat {
    kUnknown = 0,
    kS16,
    kS32,
    kFloat,
    kS16Planar,
    kFloatPlanar,
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

    int width() const;
    int height() const;
    int format() const;
    const uint8_t* PlaneData(int plane) const;
    uint8_t* PlaneData(int plane);
    int PlaneLinesize(int plane) const;

    [[nodiscard]] MediaFrame MakeWritable() const;
    static MediaFrame CreateSameFormat(const MediaFrame& ref, double pts);

    AVFrame* RawFrame() const;

  private:
    AVFramePtr frame_;
    double pts_{0.0};
    MediaType type_{MediaType::kUnknown};
};

}  // namespace mvp

#endif  // MVP_MEDIA_FRAME_H_
