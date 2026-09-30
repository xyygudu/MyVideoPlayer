#ifndef MVP_GPU_D3D11_FRAME_PRESENTER_H_
#define MVP_GPU_D3D11_FRAME_PRESENTER_H_

#include <wrl/client.h>

#include "ffmpeg_utils.h"
#include "gpu/d3d11_util.h"
#include "gpu/frame_presenter.h"

namespace mvp::gpu {

/// mpv d3d11 1-copy presentation: individual shader-resource textures (GPU
/// pass output) bind as-is; decoder array slices are copied into one texture
/// owned here. SDL binds whole non-array textures only.
class D3D11FramePresenter final : public FramePresenter {
  public:
    explicit D3D11FramePresenter(AVBufferRef* device_ref);

    void* BindableTexture(const AVFrame* hw_frame) override;

  private:
    bool EnsureCopyTexture(UINT width, UINT height, DXGI_FORMAT format);

    AVBufferRefPtr device_ref_;  // Declared first: outlives the COM objects below
    AVD3D11VADeviceContext* hwctx_{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_;
    D3D11_TEXTURE2D_DESC copy_desc_{};
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_D3D11_FRAME_PRESENTER_H_
