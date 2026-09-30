#include "gpu/d3d11_video_pass.h"

#include <spdlog/spdlog.h>

// Bytecode generated from gpu/shaders/*.hlsl by fxc at build time.
#include "gpu/shaders/kAffineRemapChromaPs.h"
#include "gpu/shaders/kAffineRemapLumaPs.h"
#include "gpu/shaders/kColorAdjustChromaPs.h"
#include "gpu/shaders/kColorAdjustLumaPs.h"
#include "gpu/shaders/kFullscreenVs.h"

namespace mvp::gpu {

struct D3D11VideoPass::PassConstants {
    float plane_size[2];    // Visible size of the plane being written
    float texture_size[2];  // Source texture plane size (decoder surfaces are padded)
    float neutral;
    float padding[3];
};

namespace {

struct KernelShaders {
    const BYTE* luma;
    SIZE_T luma_size;
    const BYTE* chroma;
    SIZE_T chroma_size;
    size_t uniform_size;
};

KernelShaders ShadersFor(VideoKernel kernel) {
    switch (kernel) {
        case VideoKernel::kColorAdjust:
            return {kColorAdjustLumaPs, sizeof(kColorAdjustLumaPs), kColorAdjustChromaPs,
                    sizeof(kColorAdjustChromaPs), sizeof(ColorAdjustUniforms)};
        case VideoKernel::kAffineRemap:
            return {kAffineRemapLumaPs, sizeof(kAffineRemapLumaPs), kAffineRemapChromaPs,
                    sizeof(kAffineRemapChromaPs), sizeof(AffineRemapUniforms)};
    }
    return {};
}

template <typename T>
void SafeRelease(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

/// Saves the pipeline state a pass overwrites and restores it on scope exit.
/// The immediate context is shared with SDL, which sets its input layout and
/// vertex shader once at device creation and never again (imgui_impl_dx11
/// backup/restore pattern).
class StateBackup {
  public:
    explicit StateBackup(ID3D11DeviceContext* ctx);
    ~StateBackup();

    StateBackup(const StateBackup&) = delete;
    StateBackup& operator=(const StateBackup&) = delete;

  private:
    static constexpr UINT kSlots = 2;  // SRV and constant-buffer slots a pass binds
    static constexpr UINT kMaxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

    ID3D11DeviceContext* ctx_;
    D3D11_PRIMITIVE_TOPOLOGY topology_{};
    ID3D11InputLayout* layout_{};
    ID3D11VertexShader* vertex_shader_{};
    ID3D11PixelShader* pixel_shader_{};
    ID3D11Buffer* constants_[kSlots]{};
    ID3D11ShaderResourceView* resources_[kSlots]{};
    ID3D11SamplerState* sampler_{};
    ID3D11RasterizerState* rasterizer_{};
    UINT viewport_count_{kMaxViewports};
    D3D11_VIEWPORT viewports_[kMaxViewports]{};
    ID3D11BlendState* blend_{};
    FLOAT blend_factor_[4]{};
    UINT sample_mask_{};
    ID3D11DepthStencilState* depth_{};
    UINT stencil_ref_{};
    ID3D11RenderTargetView* target_{};
    ID3D11DepthStencilView* depth_target_{};
};

StateBackup::StateBackup(ID3D11DeviceContext* ctx) : ctx_(ctx) {
    ctx_->IAGetPrimitiveTopology(&topology_);
    ctx_->IAGetInputLayout(&layout_);
    ctx_->VSGetShader(&vertex_shader_, nullptr, nullptr);
    ctx_->PSGetShader(&pixel_shader_, nullptr, nullptr);
    ctx_->PSGetConstantBuffers(0, kSlots, constants_);
    ctx_->PSGetShaderResources(0, kSlots, resources_);
    ctx_->PSGetSamplers(0, 1, &sampler_);
    ctx_->RSGetState(&rasterizer_);
    ctx_->RSGetViewports(&viewport_count_, viewports_);
    ctx_->OMGetBlendState(&blend_, blend_factor_, &sample_mask_);
    ctx_->OMGetDepthStencilState(&depth_, &stencil_ref_);
    ctx_->OMGetRenderTargets(1, &target_, &depth_target_);
}

StateBackup::~StateBackup() {
    ctx_->IASetPrimitiveTopology(topology_);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vertex_shader_, nullptr, 0);
    ctx_->PSSetShader(pixel_shader_, nullptr, 0);
    ctx_->PSSetConstantBuffers(0, kSlots, constants_);
    ctx_->PSSetShaderResources(0, kSlots, resources_);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    ctx_->RSSetState(rasterizer_);
    ctx_->RSSetViewports(viewport_count_, viewports_);
    ctx_->OMSetBlendState(blend_, blend_factor_, sample_mask_);
    ctx_->OMSetDepthStencilState(depth_, stencil_ref_);
    ctx_->OMSetRenderTargets(1, &target_, depth_target_);

    SafeRelease(layout_);
    SafeRelease(vertex_shader_);
    SafeRelease(pixel_shader_);
    for (UINT i = 0; i < kSlots; ++i) {
        SafeRelease(constants_[i]);
        SafeRelease(resources_[i]);
    }
    SafeRelease(sampler_);
    SafeRelease(rasterizer_);
    SafeRelease(blend_);
    SafeRelease(depth_);
    SafeRelease(target_);
    SafeRelease(depth_target_);
}

}  // namespace

std::unique_ptr<VideoPass> D3D11VideoPass::Create(AVBufferRef* device_ref, VideoKernel kernel) {
    std::unique_ptr<D3D11VideoPass> pass(new D3D11VideoPass(device_ref, kernel));
    if (!pass->CreatePipeline() || !pass->CreateStates()) {
        SPDLOG_ERROR("VideoPass(D3D11): pipeline creation failed for kernel {}",
                     static_cast<int>(kernel));
        return nullptr;
    }
    return pass;
}

D3D11VideoPass::D3D11VideoPass(AVBufferRef* device_ref, VideoKernel kernel)
    : device_ref_(av_buffer_ref(device_ref)),
      hwctx_(d3d11::DeviceHwctx(device_ref_.get())),
      kernel_(kernel) {}

bool D3D11VideoPass::CreatePipeline() {
    const KernelShaders shaders = ShadersFor(kernel_);
    uniform_size_ = shaders.uniform_size;
    ID3D11Device* device = hwctx_->device;
    if (FAILED(device->CreateVertexShader(kFullscreenVs, sizeof(kFullscreenVs), nullptr,
                                          &vertex_shader_)) ||
        FAILED(device->CreatePixelShader(shaders.luma, shaders.luma_size, nullptr,
                                         &luma_shader_)) ||
        FAILED(device->CreatePixelShader(shaders.chroma, shaders.chroma_size, nullptr,
                                         &chroma_shader_))) {
        return false;
    }
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.ByteWidth = sizeof(PassConstants);
    if (FAILED(device->CreateBuffer(&desc, nullptr, &pass_constants_))) {
        return false;
    }
    desc.ByteWidth = static_cast<UINT>(uniform_size_);
    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &kernel_constants_));
}

bool D3D11VideoPass::CreateStates() {
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;

    ID3D11Device* device = hwctx_->device;
    return SUCCEEDED(device->CreateSamplerState(&sampler, &sampler_)) &&
           SUCCEEDED(device->CreateRasterizerState(&raster, &rasterizer_));
}

MediaFrame D3D11VideoPass::Run(const MediaFrame& src, const void* uniforms, size_t size) {
    if (size != uniform_size_) {
        SPDLOG_ERROR("VideoPass(D3D11): uniform size {} does not match kernel {} ({})", size,
                     static_cast<int>(kernel_), uniform_size_);
        return MediaFrame();
    }
    const AVFrame* in = src.RawFrame();
    d3d11::PlaneFormats fmt;
    if (!in || in->format != AV_PIX_FMT_D3D11 || !in->hw_frames_ctx ||
        !d3d11::LookupPlaneFormats(d3d11::FrameSwFormat(in), &fmt)) {
        return MediaFrame();
    }
    AVFramePtr out;
    if (!EnsureOutputPool(in, d3d11::FrameSwFormat(in), fmt.texture) ||
        av_hwframe_get_buffer(output_frames_.get(), out.get(), 0) < 0) {
        return MediaFrame();
    }

    d3d11::ContextLock lock(hwctx_);
    SourceViews source;
    TargetViews target;
    if (!CreateSourceViews(in, fmt, &source) || !CreateTargetViews(out.get(), fmt, &target)) {
        SPDLOG_ERROR("VideoPass(D3D11): view creation failed");
        return MediaFrame();
    }
    hwctx_->device_context->UpdateSubresource(kernel_constants_.Get(), 0, nullptr, uniforms, 0,
                                              0);
    {
        StateBackup backup(hwctx_->device_context);
        DrawPlanes(source, target, in, fmt.neutral);
    }
    av_frame_copy_props(out.get(), in);
    out->width = in->width;  // The pool rounds texture sizes up to even
    out->height = in->height;
    return MediaFrame(out.get());
}

bool D3D11VideoPass::EnsureOutputPool(const AVFrame* src, int sw_format,
                                      DXGI_FORMAT texture_format) {
    const int width = static_cast<int>(d3d11::Align2(src->width));
    const int height = static_cast<int>(d3d11::Align2(src->height));
    if (output_frames_) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(output_frames_->data);
        if (frames->width == width && frames->height == height &&
            frames->sw_format == sw_format) {
            return true;
        }
        output_frames_.reset();
    }
    if (!d3d11::SupportsFormat(hwctx_->device, texture_format,
                               D3D11_FORMAT_SUPPORT_RENDER_TARGET)) {
        return false;  // e.g. no P010 render targets: the node falls back to CPU
    }
    AVBufferRefPtr frames_ref(av_hwframe_ctx_alloc(device_ref_.get()));
    if (!frames_ref) {
        return false;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    frames->format = AV_PIX_FMT_D3D11;
    frames->sw_format = static_cast<AVPixelFormat>(sw_format);
    frames->width = width;
    frames->height = height;
    frames->initial_pool_size = 0;  // Individual textures, recycled by reference count
    static_cast<AVD3D11VAFramesContext*>(frames->hwctx)->BindFlags =
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (av_hwframe_ctx_init(frames_ref.get()) < 0) {
        SPDLOG_ERROR("VideoPass(D3D11): output pool {}x{} init failed", width, height);
        return false;
    }
    output_frames_ = std::move(frames_ref);
    return true;
}

bool D3D11VideoPass::CreateSourceViews(const AVFrame* src, const d3d11::PlaneFormats& fmt,
                                       SourceViews* views) {
    ID3D11Texture2D* texture = d3d11::FrameTexture(src);
    UINT index = d3d11::FrameIndex(src);
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        texture = CopyToScratch(texture, index, desc);
        index = 0;
        if (!texture) {
            return false;
        }
    }
    views->texture_width = desc.Width;
    views->texture_height = desc.Height;

    // Array views read decoder slices in place; plain textures are arrays of one.
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.MipLevels = 1;
    view.Texture2DArray.FirstArraySlice = index;
    view.Texture2DArray.ArraySize = 1;
    ID3D11Device* device = hwctx_->device;
    view.Format = fmt.luma;
    if (FAILED(device->CreateShaderResourceView(texture, &view, &views->luma))) {
        return false;
    }
    view.Format = fmt.chroma;
    return SUCCEEDED(device->CreateShaderResourceView(texture, &view, &views->chroma));
}

ID3D11Texture2D* D3D11VideoPass::CopyToScratch(ID3D11Texture2D* texture, UINT index,
                                               const D3D11_TEXTURE2D_DESC& desc) {
    D3D11_TEXTURE2D_DESC current{};
    if (scratch_) {
        scratch_->GetDesc(&current);
    }
    if (!scratch_ || current.Width != desc.Width || current.Height != desc.Height ||
        current.Format != desc.Format) {
        D3D11_TEXTURE2D_DESC scratch = desc;
        scratch.MipLevels = 1;
        scratch.ArraySize = 1;
        scratch.Usage = D3D11_USAGE_DEFAULT;
        scratch.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        scratch.CPUAccessFlags = 0;
        scratch.MiscFlags = 0;
        scratch_.Reset();
        if (FAILED(hwctx_->device->CreateTexture2D(&scratch, nullptr, &scratch_))) {
            SPDLOG_ERROR("VideoPass(D3D11): scratch texture creation failed");
            return nullptr;
        }
    }
    hwctx_->device_context->CopySubresourceRegion(scratch_.Get(), 0, 0, 0, 0, texture, index,
                                                  nullptr);
    return scratch_.Get();
}

bool D3D11VideoPass::CreateTargetViews(const AVFrame* dst, const d3d11::PlaneFormats& fmt,
                                       TargetViews* views) {
    // On a 4:2:0 texture an R8/R16 view is the luma plane, R8G8/R16G16 the chroma plane.
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11Device* device = hwctx_->device;
    ID3D11Texture2D* texture = d3d11::FrameTexture(dst);
    view.Format = fmt.luma;
    if (FAILED(device->CreateRenderTargetView(texture, &view, &views->luma))) {
        return false;
    }
    view.Format = fmt.chroma;
    return SUCCEEDED(device->CreateRenderTargetView(texture, &view, &views->chroma));
}

void D3D11VideoPass::DrawPlanes(const SourceViews& src, const TargetViews& dst,
                                const AVFrame* frame, float neutral) {
    ID3D11DeviceContext* ctx = hwctx_->device_context;
    ID3D11ShaderResourceView* resources[] = {src.luma.Get(), src.chroma.Get()};
    ID3D11Buffer* constants[] = {pass_constants_.Get(), kernel_constants_.Get()};
    ID3D11SamplerState* sampler = sampler_.Get();
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    ctx->RSSetState(rasterizer_.Get());
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    // Before binding targets: a recycled output texture may still sit in SDL's SRV slots.
    ctx->PSSetShaderResources(0, 2, resources);
    ctx->PSSetConstantBuffers(0, 2, constants);
    ctx->PSSetSamplers(0, 1, &sampler);

    const float texture_w = static_cast<float>(src.texture_width);
    const float texture_h = static_cast<float>(src.texture_height);
    const UINT target_w = d3d11::Align2(frame->width);
    const UINT target_h = d3d11::Align2(frame->height);
    const PassConstants luma{{static_cast<float>(frame->width), static_cast<float>(frame->height)},
                             {texture_w, texture_h}, neutral, {}};
    DrawPlane(luma_shader_.Get(), dst.luma.Get(), luma, target_w, target_h);
    const PassConstants chroma{{static_cast<float>((frame->width + 1) / 2),
                                static_cast<float>((frame->height + 1) / 2)},
                               {texture_w / 2.0f, texture_h / 2.0f}, neutral, {}};
    DrawPlane(chroma_shader_.Get(), dst.chroma.Get(), chroma, target_w / 2, target_h / 2);

    ID3D11ShaderResourceView* no_resources[2] = {};
    ctx->PSSetShaderResources(0, 2, no_resources);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11VideoPass::DrawPlane(ID3D11PixelShader* shader, ID3D11RenderTargetView* target,
                               const PassConstants& constants, UINT width, UINT height) {
    static_assert(sizeof(PassConstants) == 32, "cbuffer layout");
    ID3D11DeviceContext* ctx = hwctx_->device_context;
    ctx->UpdateSubresource(pass_constants_.Get(), 0, nullptr, &constants, 0, 0);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width),
                                  static_cast<float>(height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &viewport);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->PSSetShader(shader, nullptr, 0);
    ctx->Draw(3, 0);
}

}  // namespace mvp::gpu
