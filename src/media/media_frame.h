#ifndef MVP_MEDIA_FRAME_H_
#define MVP_MEDIA_FRAME_H_

#include "ffmpeg_utils.h"

struct AVFrame;
struct AVBufferPool;

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
    // Software formats: pixel data in system memory.
    kYUV420P,
    kYUV422P,
    kYUV444P,
    kNV12,
    kRGB32,
    // Hardware frame domains: pixel data in GPU memory, mirroring FFmpeg's
    // hardware AVPixelFormat variants. The negotiated domain names the owning
    // device; nodes treat all of these uniformly as "hardware".
    kD3D11,  // D3D11VA, data[0] is an ID3D11Texture2D*
    kCuda,
    kQsv,
    kVAAPI,
    kVideoToolbox,
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

/// Internal frame payload for pipeline transport: owns an AVFrame and exposes
/// its pixel/sample planes. Move-only.
///
/// Carries no timestamp and no media type — both are transport metadata owned
/// by MediaBuffer. `RawFrame()->pts` is only meaningful at the FFmpeg
/// boundaries (decoder output, encoder input); frames minted by MediaFramePool
/// mid-pipeline leave it at AV_NOPTS_VALUE.
///
/// NOT exposed in public API — VideoFrame/AudioFrame remain the public types.
class MediaFrame {
  public:
    MediaFrame();
    explicit MediaFrame(AVFrame* src);
    ~MediaFrame();

    MediaFrame(MediaFrame&& other) noexcept;
    MediaFrame& operator=(MediaFrame&& other) noexcept;

    MediaFrame(const MediaFrame&) = delete;
    MediaFrame& operator=(const MediaFrame&) = delete;

    bool IsValid() const;

    /// True when pixel data lives in GPU memory (a hardware-domain frame).
    bool IsHardware() const;

    /// The software pixel format underneath a hardware frame (e.g. NV12,
    /// P010), or -1 when not a hardware frame.
    int HwSwFormat() const;

    int width() const;
    int height() const;
    int format() const;
    const uint8_t* PlaneData(int plane) const;
    uint8_t* PlaneData(int plane);
    int PlaneLinesize(int plane) const;

    // Consumes *this. If the underlying AVFrame is uniquely referenced,
    // ownership is moved out directly (zero-copy). Otherwise falls back to
    // a deep copy, same as the const& overload below.
    [[nodiscard]] MediaFrame MakeWritable() &&;
    // Leaves *this unchanged. Always returns an independent deep copy,
    // since the caller keeps using the original frame afterward.
    [[nodiscard]] MediaFrame MakeWritable() const&;

    AVFrame* RawFrame() const;

  private:
    friend class MediaFramePool;  // Acquire() assembles a MediaFrame's AVFrame directly

    AVFramePtr frame_;
};

/// GPU→CPU domain conversion: copies a hardware frame into system memory.
/// Returns an invalid frame on failure. The explicit conversion point for
/// software-only nodes (CPU effects) at their input boundary.
MediaFrame TransferToSoftware(const MediaFrame& hw_frame);

/// Reusable allocator for same-size/format output frames.
/// Wraps an AVBufferPool so repeated calls with the same width/height/format
/// (the common case for a node that re-processes the same video stream)
/// reuse already-allocated buffers instead of hitting the system allocator
/// every frame. Move-only, mirrors AVFramePtr's RAII style.
/// NOT thread-safe: intended for use by a single kPassive/kActive node that
/// calls Acquire() from one thread only (matches how effect nodes run today).
class MediaFramePool {
  public:
    MediaFramePool() = default;
    ~MediaFramePool();

    MediaFramePool(MediaFramePool&& other) noexcept;
    MediaFramePool& operator=(MediaFramePool&& other) noexcept;

    MediaFramePool(const MediaFramePool&) = delete;
    MediaFramePool& operator=(const MediaFramePool&) = delete;

    /// Returns a frame of the given size/format. Rebuilds the internal pool
    /// only when width/height/format differ from the last call.
    MediaFrame Acquire(int width, int height, int format);

  private:
    AVBufferPool* pool_{nullptr};
    int width_{0};
    int height_{0};
    int format_{-1};
};

}  // namespace mvp

#endif  // MVP_MEDIA_FRAME_H_
