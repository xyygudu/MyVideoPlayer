#ifndef MVP_GPU_GPU_DEVICE_H_
#define MVP_GPU_GPU_DEVICE_H_

#include <memory>

#include "gpu/frame_presenter.h"
#include "gpu/video_pass.h"
#include "media_frame.h"

struct AVBufferRef;
struct AVCodec;
struct AVHWFramesContext;

namespace mvp::gpu {

/// GPU device shared by the renderer that provides it and the graph that
/// uses it (GStreamer-context style). Decode, effects and presentation derive
/// their own resources from it through the factory methods below; the
/// platform code lives only in the backend implementations.
class GpuDevice {
  public:
    virtual ~GpuDevice() = default;

    /// Hardware frame domain this device produces/consumes.
    virtual PixelFormat Domain() const = 0;

    /// FFmpeg device context reference (share with codec contexts via
    /// av_buffer_ref).
    virtual AVBufferRef* DeviceRef() const = 0;

    /// Whether the codec advertises hardware decoding on this device.
    virtual bool SupportsDecoder(const AVCodec* codec) const = 0;

    /// Adjusts a decoder frame pool before av_hwframe_ctx_init so its
    /// surfaces can also feed GPU passes (mpv hwframes_refine).
    virtual void RefineDecoderFrames(AVHWFramesContext* frames) const = 0;

    /// The device command-context lock. Recursive, and the same lock FFmpeg
    /// takes internally around decode submission and transfers. Hold it for
    /// a single GPU operation only (one pass, one presentation copy + draw).
    virtual void LockContext() = 0;
    virtual void UnlockContext() = 0;

    /// nullptr when the device cannot render into video surfaces.
    virtual std::unique_ptr<VideoPass> CreateVideoPass(VideoKernel kernel) = 0;

    virtual std::unique_ptr<FramePresenter> CreatePresenter() = 0;

    /// Wrap an externally owned native device (e.g. the ID3D11Device behind
    /// an SDL3 renderer) so decode, effects and presentation share one device.
    /// Returns nullptr when the platform has no usable backend.
    static std::shared_ptr<GpuDevice> WrapExternal(void* native_device);
};

/// Holds the device context lock for a scope; no-op without a device.
class ScopedContextLock {
  public:
    explicit ScopedContextLock(GpuDevice* device) : device_(device) {
        if (device_) device_->LockContext();
    }
    ~ScopedContextLock() {
        if (device_) device_->UnlockContext();
    }

    ScopedContextLock(const ScopedContextLock&) = delete;
    ScopedContextLock& operator=(const ScopedContextLock&) = delete;

  private:
    GpuDevice* device_;
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_GPU_DEVICE_H_
