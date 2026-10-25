#include "video_renderer.h"

#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "gpu/gpu_device.h"
#include "gpu/pixel_format_map.h"

namespace mvp {

namespace {

SDL_ColorRange ToSdlRange(const AVFrame* f) {
    return IsFullRangeFrame(f) ? SDL_COLOR_RANGE_FULL : SDL_COLOR_RANGE_LIMITED;
}

// Only matrices SDL can convert (BT.601/709/2020-NCL); the rest map to the
// closest one instead of failing the draw.
SDL_MatrixCoefficients ToSdlMatrix(const AVFrame* f) {
    switch (ResolvedColorspace(f)) {
        case AVCOL_SPC_BT709:
        case AVCOL_SPC_SMPTE240M:
            return SDL_MATRIX_COEFFICIENTS_BT709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            return SDL_MATRIX_COEFFICIENTS_BT2020_NCL;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_FCC:
            return SDL_MATRIX_COEFFICIENTS_BT601;
        default:
            return IsHdFrameSize(f) ? SDL_MATRIX_COEFFICIENTS_BT709 : SDL_MATRIX_COEFFICIENTS_BT601;
    }
}

// FFmpeg and SDL both use the H.273 code points for primaries and transfer.
SDL_ColorPrimaries ToSdlPrimaries(const AVFrame* f) {
    switch (f->color_primaries) {
        case AVCOL_PRI_RESERVED0:
        case AVCOL_PRI_UNSPECIFIED:
        case AVCOL_PRI_RESERVED:
            if (IsHdFrameSize(f)) return SDL_COLOR_PRIMARIES_BT709;
            return f->height == 576 ? SDL_COLOR_PRIMARIES_BT470BG : SDL_COLOR_PRIMARIES_BT601;
        default:
            return static_cast<SDL_ColorPrimaries>(f->color_primaries);
    }
}

SDL_TransferCharacteristics ToSdlTransfer(const AVFrame* f) {
    switch (f->color_trc) {
        case AVCOL_TRC_RESERVED0:
        case AVCOL_TRC_UNSPECIFIED:
        case AVCOL_TRC_RESERVED:
            return SDL_TRANSFER_CHARACTERISTICS_BT709;  // Untagged = SDR.
        default:
            return static_cast<SDL_TransferCharacteristics>(f->color_trc);
    }
}

SDL_ChromaLocation ToSdlChroma(const AVFrame* f) {
    switch (f->chroma_location) {
        case AVCHROMA_LOC_CENTER:
            return SDL_CHROMA_LOCATION_CENTER;
        case AVCHROMA_LOC_TOPLEFT:
            return SDL_CHROMA_LOCATION_TOPLEFT;
        default:
            return SDL_CHROMA_LOCATION_LEFT;  // MPEG-2/4 and AVC default
    }
}

uint32_t MakeColorspace(SDL_ColorRange range, const AVFrame* f, SDL_MatrixCoefficients matrix) {
    return SDL_DEFINE_COLORSPACE(SDL_COLOR_TYPE_YCBCR, range, ToSdlPrimaries(f), ToSdlTransfer(f),
                                 matrix, ToSdlChroma(f));
}

// SDL defaults YUV textures to full-range BT.601 and P010 to HDR10; describe
// the frame's real colorimetry, guessing untagged fields like mpv.
uint32_t FrameColorspace(const AVFrame* f) {
    return MakeColorspace(ToSdlRange(f), f, ToSdlMatrix(f));
}

// Colorimetry of the fallback's YUV420P output: libswscale converts RGB with
// BT.601 into limited range; YUV keeps its matrix and range (yuvj sources
// are converted as their yuv twins, so their full range carries over).
uint32_t FallbackColorspace(const AVFrame* f) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    if (desc && (desc->flags & AV_PIX_FMT_FLAG_RGB)) {
        return MakeColorspace(SDL_COLOR_RANGE_LIMITED, f, SDL_MATRIX_COEFFICIENTS_BT601);
    }
    return FrameColorspace(f);
}

}  // namespace

VideoRenderer::VideoRenderer() = default;

VideoRenderer::~VideoRenderer() { Close(); }

bool VideoRenderer::Open(void* native_window_handle, int width, int height) {
    if (renderer_) {
        spdlog::warn("VideoRenderer::Open called while already open");
        return true;
    }

    window_width_ = width;
    window_height_ = height;

    // Create an SDL window from the native handle (embedded in Qt widget)
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "mvp_video");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, false);
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,
                           native_window_handle);
    window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (!window_) {
        spdlog::error("SDL_CreateWindowWithProperties failed: {}", SDL_GetError());
        return false;
    }

    if (!CreateRenderer()) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    ProbeBackend();
    return true;
}

bool VideoRenderer::CreateRenderer() {
    // Prefer the D3D11 backend: it is the only one that can bind
    // hardware-decoded textures directly for zero-copy presentation.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    // The backend device is shared with the decode thread; SDL defaults to a
    // D3D11_CREATE_DEVICE_SINGLETHREADED device.
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, nullptr);  // let SDL pick any
        renderer_ = SDL_CreateRenderer(window_, nullptr);
    }
    if (!renderer_) {
        spdlog::error("SDL_CreateRenderer failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

void VideoRenderer::ProbeBackend() {
    SDL_PropertiesID rprops = SDL_GetRendererProperties(renderer_);
    backend_name_ = SDL_GetStringProperty(rprops, SDL_PROP_RENDERER_NAME_STRING, "unknown");
    void* native_device = SDL_GetPointerProperty(
        rprops, SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr);
    // The presenting device is the one decode and GPU effects must use, so
    // hardware frames reach the screen without leaving the GPU.
    if (native_device) {
        gpu_device_ = gpu::GpuDevice::WrapExternal(native_device);
    }
    native_device_ = native_device;
    device_lost_ = false;
    present_failing_ = false;
    if (gpu_device_) {
        presenter_ = gpu_device_->CreatePresenter();
        bindable_domain_ = gpu_device_->Domain();
    }

    spdlog::info("VideoRenderer opened ({}x{}, backend '{}', hw binding {})",
                 window_width_, window_height_, backend_name_,
                 bindable_domain_ != PixelFormat::kUnknown ? "on" : "off");
}

void VideoRenderer::RecoverDevice() {
    if (!renderer_) {
        return;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        texture_width_ = 0;
        texture_height_ = 0;
    }
    presenter_.reset();
    gpu_device_.reset();
    bindable_domain_ = PixelFormat::kUnknown;
    ProbeBackend();
}

void VideoRenderer::Close() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (convert_buffer_) {
        av_free(convert_buffer_);
        convert_buffer_ = nullptr;
        convert_buffer_size_ = 0;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
        texture_width_ = 0;
        texture_height_ = 0;
    }
    // Before the SDL renderer: the device flushes its context on release.
    presenter_.reset();
    gpu_device_.reset();
    native_device_ = nullptr;
    backend_name_.clear();
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    bindable_domain_ = PixelFormat::kUnknown;
}

void VideoRenderer::Render(const MediaFrame& frame) {
    if (!renderer_ || !frame.IsValid()) return;

    // SDL, FFmpeg and GPU passes share the device's single immediate context;
    // no device = software pipeline, nothing to serialize.
    gpu::ScopedContextLock lock(gpu_device_.get());
    if (frame.IsHardware()) {
        RenderHWFrame(frame);
    } else {
        RenderSoftwareFrame(frame);
    }
}

void VideoRenderer::RenderSoftwareFrame(const MediaFrame& frame) {
    switch (gpu::FromAvPixelFormat(frame.RawFrame()->format)) {
        case PixelFormat::kNV12:
            RenderNV12(frame);
            break;
        case PixelFormat::kYUV420P:
            RenderYUV420P(frame);
            break;
        default:
            RenderFallback(frame);
            break;
    }
}

void VideoRenderer::RenderYUV420P(const MediaFrame& frame) {
    AVFrame* av = frame.RawFrame();
    int fw = av->width;
    int fh = av->height;
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_IYUV, FrameColorspace(av));
    if (!texture_) return;

    SDL_UpdateYUVTexture(texture_, nullptr,
                         av->data[0], av->linesize[0],
                         av->data[1], av->linesize[1],
                         av->data[2], av->linesize[2]);
    Present(fw, fh);
}

void VideoRenderer::RenderNV12(const MediaFrame& frame) {
    AVFrame* av = frame.RawFrame();
    int fw = av->width;
    int fh = av->height;
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_NV12, FrameColorspace(av));
    if (!texture_) return;

    SDL_UpdateNVTexture(texture_, nullptr,
                        av->data[0], av->linesize[0],
                        av->data[1], av->linesize[1]);
    Present(fw, fh);
}

void VideoRenderer::RenderHWFrame(const MediaFrame& frame) {
    void* texture = (presenter_ && zero_copy_) ? presenter_->BindableTexture(frame.RawFrame())
                                               : nullptr;
    if (texture && RenderBoundTexture(frame, texture)) {
        return;
    }
    // Unbindable layout (or zero-copy switched off): download under the
    // (recursive) device lock.
    MediaFrame sw = TransferToSoftware(frame);
    if (!sw.IsValid()) {
        SPDLOG_WARN("VideoRenderer: hardware frame not presentable, dropped");
        return;
    }
    RenderSoftwareFrame(sw);
}

bool VideoRenderer::RenderBoundTexture(const MediaFrame& frame, void* texture) {
    AVFrame* hw = frame.RawFrame();
    int sdl_format;
    switch (frame.HwSwFormat()) {
        case AV_PIX_FMT_NV12:
            sdl_format = SDL_PIXELFORMAT_NV12;
            break;
        case AV_PIX_FMT_P010:
            sdl_format = SDL_PIXELFORMAT_P010;
            break;
        default:
            return false;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER, texture);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, hw->width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,
                          hw->height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                          sdl_format);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                          FrameColorspace(hw));
    SDL_Texture* bound = SDL_CreateTextureWithProperties(renderer_, props);
    SDL_DestroyProperties(props);
    if (!bound) {
        SPDLOG_WARN("VideoRenderer: binding texture failed: {}",
                    SDL_GetError());
        return false;
    }

    // The frame (held by VideoSinkNode) keeps the texture alive through the draw.
    Present(bound, hw->width, hw->height);
    SDL_DestroyTexture(bound);
    return true;
}

void VideoRenderer::RenderFallback(const MediaFrame& frame) {
    AVFrame* av = frame.RawFrame();
    if (!av) return;

    int fw = av->width;
    int fh = av->height;
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_IYUV, FallbackColorspace(av));
    if (!texture_) return;

    // Rebuilt whenever the source size or pixel format changes.
    sws_ctx_ = sws_getCachedContext(sws_ctx_, fw, fh, WithoutJpegRange(av->format),
                                    fw, fh, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                    nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        if (sws_failed_format_ != av->format) {
            SPDLOG_WARN("VideoRenderer: no conversion from pixel format {}, frames dropped",
                        av->format);
            sws_failed_format_ = av->format;
        }
        return;
    }

    int needed = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, fw, fh, 1);
    if (needed > convert_buffer_size_) {
        if (convert_buffer_) av_free(convert_buffer_);
        convert_buffer_ = static_cast<uint8_t*>(av_malloc(needed));
        convert_buffer_size_ = convert_buffer_ ? needed : 0;
    }
    if (!convert_buffer_) return;

    uint8_t* dst_data[4] = {};
    int dst_linesize[4] = {};
    av_image_fill_arrays(dst_data, dst_linesize, convert_buffer_,
                         AV_PIX_FMT_YUV420P, fw, fh, 1);

    sws_scale(sws_ctx_, av->data, av->linesize, 0, fh, dst_data, dst_linesize);

    SDL_UpdateYUVTexture(texture_, nullptr,
                         dst_data[0], dst_linesize[0],
                         dst_data[1], dst_linesize[1],
                         dst_data[2], dst_linesize[2]);
    Present(fw, fh);
}

void VideoRenderer::Present(int frame_width, int frame_height) {
    Present(texture_, frame_width, frame_height);
}

void VideoRenderer::Present(SDL_Texture* texture, int frame_width,
                            int frame_height) {
    RenderClear();
    float dx, dy, dw, dh;
    ComputeDestRect(frame_width, frame_height, &dx, &dy, &dw, &dh);
    SDL_FRect dst{dx, dy, dw, dh};
    SDL_RenderTexture(renderer_, texture, nullptr, &dst);
    CheckPresented(SDL_RenderPresent(renderer_));
}

void VideoRenderer::CheckPresented(bool presented) {
    if (presented) {
        present_failing_ = false;
        return;
    }
    if (device_lost_) {
        return;
    }
    // After a device removal SDL recreates its D3D11 device; decode and the
    // GPU passes still use the old one, so this renderer cannot recover alone.
    void* device = SDL_GetPointerProperty(SDL_GetRendererProperties(renderer_),
                                          SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr);
    if (native_device_ && device != native_device_) {
        device_lost_ = true;
        spdlog::error("VideoRenderer: GPU device was reset ({}), reopen required", SDL_GetError());
        return;
    }
    if (!present_failing_) {
        spdlog::warn("VideoRenderer: SDL_RenderPresent failed: {}", SDL_GetError());
        present_failing_ = true;
    }
}

void VideoRenderer::Resize(int width, int height) {
    window_width_ = width;
    window_height_ = height;
}

void VideoRenderer::EnsureTexture(int frame_width, int frame_height, int sdl_format,
                                  uint32_t colorspace) {
    if (texture_ && texture_width_ == frame_width && texture_height_ == frame_height &&
        texture_format_ == sdl_format && texture_colorspace_ == colorspace) {
        return;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, sdl_format);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                          SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame_width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame_height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);
    texture_ = SDL_CreateTextureWithProperties(renderer_, props);
    SDL_DestroyProperties(props);
    if (!texture_) {
        spdlog::error("SDL_CreateTextureWithProperties failed: {}", SDL_GetError());
        return;
    }
    texture_width_ = frame_width;
    texture_height_ = frame_height;
    texture_format_ = sdl_format;
    texture_colorspace_ = colorspace;
}

void VideoRenderer::RenderClear() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
}

void VideoRenderer::ComputeDestRect(int frame_width, int frame_height,
                                     float* x, float* y, float* w, float* h) const {
    // SDL's real output size in pixels; the size reported by the UI is in
    // logical units and is wrong under display scaling.
    int out_w = 0;
    int out_h = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer_, &out_w, &out_h) || out_w <= 0 || out_h <= 0) {
        out_w = window_width_;
        out_h = window_height_;
    }
    const float win_w = static_cast<float>(out_w);
    const float win_h = static_cast<float>(out_h);
    float src_aspect = static_cast<float>(frame_width) / static_cast<float>(frame_height);
    float dst_aspect = win_w / win_h;

    float dst_w, dst_h;
    if (src_aspect > dst_aspect) {
        // Letterbox (bars top/bottom)
        dst_w = win_w;
        dst_h = dst_w / src_aspect;
    } else {
        // Pillarbox (bars left/right)
        dst_h = win_h;
        dst_w = dst_h * src_aspect;
    }

    *x = (win_w - dst_w) / 2.0f;
    *y = (win_h - dst_h) / 2.0f;
    *w = dst_w;
    *h = dst_h;
}

}  // namespace mvp
