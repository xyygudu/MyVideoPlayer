#include "gpu/d3d11_device.h"

#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
}
#include <spdlog/spdlog.h>

#include "gpu/d3d11_frame_presenter.h"
#include "gpu/d3d11_util.h"
#include "gpu/d3d11_video_pass.h"
// After <d3d11.h> (pulled in above): d3d10_1.h rejects an earlier d3d10.h.
#include <d3d10.h>

namespace mvp::gpu {

namespace {

// Refcounted AVFrames release D3D11 textures on whichever thread drops the
// last reference (sink, control), outside any app-level lock. FFmpeg, mpv and
// VLC all enable this on devices shared between decode and presentation.
// Fails for devices created with D3D11_CREATE_DEVICE_SINGLETHREADED.
bool EnableMultithreadProtection(ID3D11Device* device) {
    ID3D10Multithread* mt = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&mt))) || !mt) {
        return false;
    }
    const BOOL was_on = mt->GetMultithreadProtected();
    mt->SetMultithreadProtected(TRUE);
    mt->Release();
    SPDLOG_INFO("GpuDevice(D3D11): multithread protection {}",
                was_on ? "already on" : "was off, enabled");
    return true;
}

// FFmpeg requires the context lock to be recursive (hwcontext_d3d11va.h).
void LockMutex(void* mutex) { static_cast<std::recursive_mutex*>(mutex)->lock(); }
void UnlockMutex(void* mutex) { static_cast<std::recursive_mutex*>(mutex)->unlock(); }

// Frames, frame pools and codecs reference the FFmpeg device context and can
// outlive the GpuDevice wrapper, so the lock dies with the context instead.
void FreeMutex(AVHWDeviceContext* ctx) {
    delete static_cast<std::recursive_mutex*>(ctx->user_opaque);
}

// The lock must be installed before av_hwdevice_ctx_init; otherwise FFmpeg
// creates a private one and app-side GPU work would be unsynchronized with it.
AVBufferRef* WrapWithSharedLock(ID3D11Device* device) {
    AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!ref) {
        return nullptr;
    }
    auto* device_ctx = reinterpret_cast<AVHWDeviceContext*>(ref->data);
    auto* hwctx = static_cast<AVD3D11VADeviceContext*>(device_ctx->hwctx);
    auto* mutex = new std::recursive_mutex();
    device_ctx->user_opaque = mutex;
    device_ctx->free = &FreeMutex;
    hwctx->lock_ctx = mutex;
    hwctx->lock = &LockMutex;
    hwctx->unlock = &UnlockMutex;
    hwctx->device = device;
    device->AddRef();
    if (av_hwdevice_ctx_init(ref) < 0) {
        av_buffer_unref(&ref);
    }
    return ref;
}

}  // namespace

D3D11GpuDevice::D3D11GpuDevice(AVBufferRef* device_ref, bool video_passes_supported)
    : device_ref_(device_ref), video_passes_supported_(video_passes_supported) {}

D3D11GpuDevice::~D3D11GpuDevice() {
    // Submit what the last frames queued before the SDL renderer teardown.
    AVD3D11VADeviceContext* hwctx = d3d11::DeviceHwctx(device_ref_);
    {
        d3d11::ContextLock lock(hwctx);
        hwctx->device_context->Flush();
    }
    av_buffer_unref(&device_ref_);
}

std::shared_ptr<GpuDevice> D3D11GpuDevice::Wrap(void* native_device) {
    auto* device = static_cast<ID3D11Device*>(native_device);
    if (!device) {
        SPDLOG_WARN("GpuDevice(D3D11): null native device");
        return nullptr;
    }
    if (!EnableMultithreadProtection(device)) {
        SPDLOG_WARN("GpuDevice(D3D11): device is single-threaded, cannot be "
                    "shared across decode/render threads; hw decode disabled");
        return nullptr;
    }
    AVBufferRef* ref = WrapWithSharedLock(device);
    if (!ref) {
        SPDLOG_WARN("GpuDevice(D3D11): wrapping the external device failed");
        return nullptr;
    }
    const bool passes = d3d11::SupportsFormat(
        device, DXGI_FORMAT_NV12,
        D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    SPDLOG_INFO("GpuDevice(D3D11): wrapped, GPU video passes {}",
                passes ? "available" : "unavailable (no NV12 render target)");
    return std::shared_ptr<GpuDevice>(new D3D11GpuDevice(ref, passes));
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

void D3D11GpuDevice::RefineDecoderFrames(AVHWFramesContext* frames) const {
    d3d11::PlaneFormats fmt;
    if (frames->format != AV_PIX_FMT_D3D11 ||
        !d3d11::LookupPlaneFormats(frames->sw_format, &fmt)) {
        return;
    }
    ID3D11Device* device = d3d11::DeviceHwctx(device_ref_)->device;
    if (!d3d11::SupportsFormat(device, fmt.texture, D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
        return;
    }
    auto* frames_hwctx = static_cast<AVD3D11VAFramesContext*>(frames->hwctx);
    frames_hwctx->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
}

void D3D11GpuDevice::LockContext() {
    const AVD3D11VADeviceContext* hwctx = d3d11::DeviceHwctx(device_ref_);
    hwctx->lock(hwctx->lock_ctx);
}

void D3D11GpuDevice::UnlockContext() {
    const AVD3D11VADeviceContext* hwctx = d3d11::DeviceHwctx(device_ref_);
    hwctx->unlock(hwctx->lock_ctx);
}

std::unique_ptr<VideoPass> D3D11GpuDevice::CreateVideoPass(VideoKernel kernel) {
    if (!video_passes_supported_) {
        return nullptr;
    }
    return D3D11VideoPass::Create(device_ref_, kernel);
}

std::unique_ptr<FramePresenter> D3D11GpuDevice::CreatePresenter() {
    return std::make_unique<D3D11FramePresenter>(device_ref_);
}

}  // namespace mvp::gpu
