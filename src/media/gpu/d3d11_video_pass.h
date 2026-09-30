#ifndef MVP_GPU_D3D11_VIDEO_PASS_H_
#define MVP_GPU_D3D11_VIDEO_PASS_H_

#include <memory>

#include <wrl/client.h>

#include "ffmpeg_utils.h"
#include "gpu/d3d11_util.h"
#include "gpu/video_pass.h"

namespace mvp::gpu {

/// Runs a VideoKernel with HLSL pixel shaders: one full-screen triangle per
/// output plane (luma, then interleaved chroma). Sources are read through
/// Texture2DArray views, so decoder array slices need no copy; outputs are
/// individual render-target textures from an FFmpeg frame pool, recycled by
/// reference count.
class D3D11VideoPass final : public VideoPass {
  public:
    /// nullptr when the kernel's pipeline cannot be created on this device.
    static std::unique_ptr<VideoPass> Create(AVBufferRef* device_ref, VideoKernel kernel);

    MediaFrame Run(const MediaFrame& src, const void* uniforms, size_t size) override;

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct SourceViews {
        ComPtr<ID3D11ShaderResourceView> luma;
        ComPtr<ID3D11ShaderResourceView> chroma;
        UINT texture_width{0};
        UINT texture_height{0};
    };
    struct TargetViews {
        ComPtr<ID3D11RenderTargetView> luma;
        ComPtr<ID3D11RenderTargetView> chroma;
    };
    struct PassConstants;  // Mirrors cbuffer PassConstants (video_pass_common.hlsli)

    D3D11VideoPass(AVBufferRef* device_ref, VideoKernel kernel);

    bool CreatePipeline();
    bool CreateStates();
    bool EnsureOutputPool(const AVFrame* src, int sw_format, DXGI_FORMAT texture_format);
    bool CreateSourceViews(const AVFrame* src, const d3d11::PlaneFormats& fmt,
                           SourceViews* views);
    ID3D11Texture2D* CopyToScratch(ID3D11Texture2D* texture, UINT index,
                                   const D3D11_TEXTURE2D_DESC& desc);
    bool CreateTargetViews(const AVFrame* dst, const d3d11::PlaneFormats& fmt,
                           TargetViews* views);
    void DrawPlanes(const SourceViews& src, const TargetViews& dst, const AVFrame* frame,
                    float neutral);
    void DrawPlane(ID3D11PixelShader* shader, ID3D11RenderTargetView* target,
                   const PassConstants& constants, UINT width, UINT height);

    AVBufferRefPtr device_ref_;  // Declared first: outlives the COM objects below
    AVD3D11VADeviceContext* hwctx_{nullptr};
    VideoKernel kernel_;
    size_t uniform_size_{0};

    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> luma_shader_;
    ComPtr<ID3D11PixelShader> chroma_shader_;
    ComPtr<ID3D11Buffer> pass_constants_;
    ComPtr<ID3D11Buffer> kernel_constants_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    // Shader-readable copy for sources whose pool lacks D3D11_BIND_SHADER_RESOURCE.
    ComPtr<ID3D11Texture2D> scratch_;
    AVBufferRefPtr output_frames_;
};

}  // namespace mvp::gpu

#endif  // MVP_GPU_D3D11_VIDEO_PASS_H_
