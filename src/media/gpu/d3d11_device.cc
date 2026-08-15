#include "gpu/d3d11_device.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}
// Pulls in <d3d11.h>; must stay in C++ linkage (operator overloads).
#include <libavutil/hwcontext_d3d11va.h>
#include <spdlog/spdlog.h>

namespace mvp::gpu {

D3D11GpuDevice::D3D11GpuDevice(AVBufferRef* device_ref)
    : device_ref_(device_ref) {}

D3D11GpuDevice::~D3D11GpuDevice() { av_buffer_unref(&device_ref_); }

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

}  // namespace mvp::gpu
