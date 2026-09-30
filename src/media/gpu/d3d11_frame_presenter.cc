#include "gpu/d3d11_frame_presenter.h"

#include <spdlog/spdlog.h>

namespace mvp::gpu {

D3D11FramePresenter::D3D11FramePresenter(AVBufferRef* device_ref)
    : device_ref_(av_buffer_ref(device_ref)),
      hwctx_(d3d11::DeviceHwctx(device_ref_.get())) {}

void* D3D11FramePresenter::BindableTexture(const AVFrame* frame) {
    d3d11::PlaneFormats fmt;
    if (!frame || frame->format != AV_PIX_FMT_D3D11 || !frame->hw_frames_ctx ||
        !d3d11::LookupPlaneFormats(d3d11::FrameSwFormat(frame), &fmt)) {
        return nullptr;
    }

    ID3D11Texture2D* texture = d3d11::FrameTexture(frame);
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    const UINT width = d3d11::Align2(frame->width);
    const UINT height = d3d11::Align2(frame->height);
    // SDL wraps the whole texture and derives its size from the frame size.
    if (desc.ArraySize == 1 && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
        desc.Width == width && desc.Height == height) {
        return texture;
    }

    if (!EnsureCopyTexture(width, height, fmt.texture)) {
        return nullptr;
    }
    // Decoder surfaces are padded (16/128-aligned); copy the visible area only.
    const D3D11_BOX box{0, 0, 0, width, height, 1};
    hwctx_->device_context->CopySubresourceRegion(copy_.Get(), 0, 0, 0, 0, texture,
                                                  d3d11::FrameIndex(frame), &box);
    return copy_.Get();
}

bool D3D11FramePresenter::EnsureCopyTexture(UINT width, UINT height, DXGI_FORMAT format) {
    if (copy_ && copy_desc_.Width == width && copy_desc_.Height == height &&
        copy_desc_.Format == format) {
        return true;
    }
    copy_.Reset();
    copy_desc_ = {};
    copy_desc_.Width = width;
    copy_desc_.Height = height;
    copy_desc_.MipLevels = 1;
    copy_desc_.ArraySize = 1;
    copy_desc_.Format = format;
    copy_desc_.SampleDesc.Count = 1;
    copy_desc_.Usage = D3D11_USAGE_DEFAULT;
    copy_desc_.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const HRESULT hr = hwctx_->device->CreateTexture2D(&copy_desc_, nullptr, &copy_);
    if (FAILED(hr)) {
        SPDLOG_ERROR("FramePresenter(D3D11): copy texture {}x{} failed (0x{:08x})", width,
                     height, static_cast<unsigned>(hr));
        copy_.Reset();
        return false;
    }
    return true;
}

}  // namespace mvp::gpu
