#ifndef MVP_GPU_D3D11_UTIL_H_
#define MVP_GPU_D3D11_UTIL_H_

#include <cstdint>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}
// Pulls in <d3d11.h>; must stay in C++ linkage (operator overloads).
#include <libavutil/hwcontext_d3d11va.h>

namespace mvp::gpu::d3d11 {

/// DXGI layout of a two-plane YUV frame and the per-plane view formats used
/// to read or write each plane from shaders.
struct PlaneFormats {
    DXGI_FORMAT texture{DXGI_FORMAT_UNKNOWN};
    DXGI_FORMAT luma{DXGI_FORMAT_UNKNOWN};
    DXGI_FORMAT chroma{DXGI_FORMAT_UNKNOWN};
    float neutral{0.5f};  // Mid-grey code value, normalized
};

/// NV12 and P010 only; false for any other software layout.
inline bool LookupPlaneFormats(int sw_format, PlaneFormats* out) {
    switch (sw_format) {
        case AV_PIX_FMT_NV12:
            *out = {DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM,
                    128.0f / 255.0f};
            return true;
        case AV_PIX_FMT_P010:
            *out = {DXGI_FORMAT_P010, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM,
                    32768.0f / 65535.0f};
            return true;
        default:
            return false;
    }
}

/// 4:2:0 textures require even dimensions.
inline UINT Align2(int value) { return static_cast<UINT>((value + 1) & ~1); }

inline AVD3D11VADeviceContext* DeviceHwctx(AVBufferRef* device_ref) {
    auto* ctx = reinterpret_cast<AVHWDeviceContext*>(device_ref->data);
    return static_cast<AVD3D11VADeviceContext*>(ctx->hwctx);
}

inline ID3D11Texture2D* FrameTexture(const AVFrame* frame) {
    return reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
}

/// Array slice holding the frame (0 for individual textures).
inline UINT FrameIndex(const AVFrame* frame) {
    return static_cast<UINT>(reinterpret_cast<intptr_t>(frame->data[1]));
}

inline int FrameSwFormat(const AVFrame* frame) {
    return reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data)->sw_format;
}

inline bool SupportsFormat(ID3D11Device* device, DXGI_FORMAT format, UINT required) {
    UINT support = 0;
    return SUCCEEDED(device->CheckFormatSupport(format, &support)) &&
           (support & required) == required;
}

/// Holds the device command-context lock, the same one FFmpeg uses.
class ContextLock {
  public:
    explicit ContextLock(const AVD3D11VADeviceContext* hwctx) : hwctx_(hwctx) {
        hwctx_->lock(hwctx_->lock_ctx);
    }
    ~ContextLock() { hwctx_->unlock(hwctx_->lock_ctx); }

    ContextLock(const ContextLock&) = delete;
    ContextLock& operator=(const ContextLock&) = delete;

  private:
    const AVD3D11VADeviceContext* hwctx_;
};

}  // namespace mvp::gpu::d3d11

#endif  // MVP_GPU_D3D11_UTIL_H_
