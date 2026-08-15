#ifndef MVP_GPU_GPU_DEVICE_H_
#define MVP_GPU_GPU_DEVICE_H_

#include <memory>

#include "media_frame.h"

struct AVBufferRef;
struct AVCodec;

namespace mvp::gpu {

/// GPU device shared by an entire graph (GStreamer-context style). The
/// pipeline builder wraps the platform/backend device and injects it into
/// MediaGraph before negotiation; decode, encode and effects then derive
/// their own per-context resources from it. The graph is the sole owner;
/// nodes hold non-owning pointers valid for the graph's lifetime.
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

    /// Wrap an externally owned native device (e.g. the ID3D11Device behind
    /// an SDL3 renderer) so decode and presentation share one device.
    /// Returns nullptr when the platform has no usable backend.
    static std::unique_ptr<GpuDevice> WrapExternal(void* native_device);
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_GPU_DEVICE_H_
