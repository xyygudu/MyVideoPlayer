#ifndef MVP_GPU_GPU_DEVICE_H_
#define MVP_GPU_GPU_DEVICE_H_

#include <memory>
#include <mutex>

#include "media_frame.h"

struct AVBufferRef;
struct AVCodec;
struct AVFrame;

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

    /// Copies a decoded hardware frame into an individual texture owned by
    /// the device, suitable for direct presentation binding. Decoder frames
    /// live in array textures that presentation APIs cannot wrap, so this
    /// is a GPU-side blit (mpv d3d11 interop pattern). Returns nullptr when
    /// the frame layout is unsupported — the caller then converts on its own
    /// thread via av_hwframe_transfer_data.
    ///
    /// Thread contract: SHALL be called on the decode thread only. The
    /// device's command context is not thread-safe; keeping every device
    /// operation on the thread that submits decode work is what keeps it
    /// single-threaded (same model as the ffmpeg CLI).
    virtual void* CopyForPresentation(const AVFrame* hw_frame) = 0;

    /// Mutex serializing the device's single immediate context, which is
    /// shared by FFmpeg decode/copy (decode thread) and the renderer backend
    /// (render thread) — the D3D11 device hands out ONE immediate context to
    /// every caller, so all command submission must be mutually exclusive
    /// (mpv's d3d11 ctx_lock model). Nodes take it around FFmpeg codec calls;
    /// the renderer takes it around draw operations.
    virtual std::mutex& DeviceContextMutex() = 0;

    /// Wrap an externally owned native device (e.g. the ID3D11Device behind
    /// an SDL3 renderer) so decode and presentation share one device.
    /// Returns nullptr when the platform has no usable backend.
    static std::unique_ptr<GpuDevice> WrapExternal(void* native_device);
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_GPU_DEVICE_H_
