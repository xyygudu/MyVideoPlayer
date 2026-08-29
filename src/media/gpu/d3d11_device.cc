#include "gpu/d3d11_device.h"

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}
// Pulls in <d3d11.h>; must stay in C++ linkage (operator overloads).
#include <libavutil/hwcontext_d3d11va.h>
#include <spdlog/spdlog.h>

namespace mvp::gpu {

namespace {
// Presentation pool ring size: must exceed the frames a sink and the links
// can hold simultaneously (link depth 3 + current frame), so a texture is
// only reused after every outstanding reference to it has been presented.
constexpr size_t kPoolSize = 8;
}  // namespace

D3D11GpuDevice::D3D11GpuDevice(AVBufferRef* device_ref)
    : device_ref_(device_ref) {
    auto* device_ctx = reinterpret_cast<AVHWDeviceContext*>(device_ref_->data);
    auto* hwctx =
        reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    device_context_ = hwctx->device_context;
}

D3D11GpuDevice::~D3D11GpuDevice() {
    // Flush the immediate context before releasing the presentation pool:
    // queued blits from the last frames may still reference pool textures.
    // Releasing them mid-queue leaves the D3D11 runtime freeing textures the
    // SDL renderer teardown later touches → access violation at Close().
    if (device_context_) {
        device_context_->Flush();
    }
    for (ID3D11Texture2D* tex : pool_) {
        tex->Release();
    }
    pool_.clear();
    av_buffer_unref(&device_ref_);
}

std::unique_ptr<GpuDevice> D3D11GpuDevice::Wrap(void* native_device) {
    if (!native_device) {
        SPDLOG_WARN("GpuDevice(D3D11): null native device");
        return nullptr;
    }

    AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        SPDLOG_WARN("GpuDevice(D3D11): av_hwdevice_ctx_alloc failed");
        return nullptr;
    }
    auto* device_ctx = reinterpret_cast<AVHWDeviceContext*>(ref->data);
    auto* hwctx = reinterpret_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    hwctx->device = static_cast<ID3D11Device*>(native_device);
    if (av_hwdevice_ctx_init(ref) < 0) {
        av_buffer_unref(&ref);
        SPDLOG_WARN("GpuDevice(D3D11): wrapping the external device failed");
        return nullptr;
    }
    return std::unique_ptr<GpuDevice>(new D3D11GpuDevice(ref));
}

bool D3D11GpuDevice::SupportsDecoder(const AVCodec* codec) const {
    if (!codec) return false;
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return false;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
            return true;
        }
    }
}

void* D3D11GpuDevice::CopyForPresentation(const AVFrame* hw_frame) {
    if (!hw_frame || hw_frame->format != AV_PIX_FMT_D3D11 ||
        !hw_frame->data[0] || !hw_frame->hw_frames_ctx) {
        return nullptr;
    }

    const AVHWFramesContext* fctx = reinterpret_cast<const AVHWFramesContext*>(
        hw_frame->hw_frames_ctx->data);
    int dxgi_format;
    switch (fctx ? fctx->sw_format : -1) {
        case AV_PIX_FMT_NV12: dxgi_format = DXGI_FORMAT_NV12; break;
        case AV_PIX_FMT_P010: dxgi_format = DXGI_FORMAT_P010; break;
        default: return nullptr;  // Unsupported layout: caller converts.
    }

    auto* src = reinterpret_cast<ID3D11Texture2D*>(hw_frame->data[0]);
    UINT src_index = static_cast<UINT>(reinterpret_cast<intptr_t>(
        hw_frame->data[1]));

    // Caller (DecoderNode::DeviceLock) already holds DeviceContextMutex(): this
    // blit submits to the same immediate context as the decode work, which must
    // stay mutually exclusive with render-thread draw submission. Never lock
    // the device mutex here — the caller already owns it (non-recursive).
    ID3D11Texture2D* dst = AcquirePoolTexture(hw_frame->width, hw_frame->height,
                                             dxgi_format);
    if (!dst) {
        return nullptr;
    }
    // Same command context that submitted the decode work: submission order
    // guarantees the blit runs after the surface is fully decoded.
    device_context_->CopySubresourceRegion(dst, 0, 0, 0, 0, src, src_index,
                                           nullptr);
    return dst;
}

ID3D11Texture2D* D3D11GpuDevice::AcquirePoolTexture(int width, int height,
                                                    int dxgi_format) {
    if (width != pool_width_ || height != pool_height_ ||
        dxgi_format != pool_format_) {
        for (ID3D11Texture2D* tex : pool_) {
            tex->Release();
        }
        pool_.clear();
        pool_next_ = 0;

        auto* device_ctx =
            reinterpret_cast<AVHWDeviceContext*>(device_ref_->data);
        auto* hwctx = reinterpret_cast<AVD3D11VADeviceContext*>(
            device_ctx->hwctx);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = static_cast<DXGI_FORMAT>(dxgi_format);
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        for (size_t i = 0; i < kPoolSize; ++i) {
            ID3D11Texture2D* tex = nullptr;
            if (FAILED(hwctx->device->CreateTexture2D(&desc, nullptr, &tex)) ||
                !tex) {
                SPDLOG_ERROR("GpuDevice(D3D11): presentation pool alloc failed");
                break;
            }
            pool_.push_back(tex);
        }
        if (pool_.empty()) {
            return nullptr;
        }
        pool_width_ = width;
        pool_height_ = height;
        pool_format_ = dxgi_format;
    }

    ID3D11Texture2D* tex = pool_[pool_next_];
    pool_next_ = (pool_next_ + 1) % pool_.size();
    return tex;
}

}  // namespace mvp::gpu
