#include "video_renderer.h"

#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"

namespace mvp {

namespace {

inline PixelFormat MapPixelFormat(int av_pix_fmt) {
    switch (av_pix_fmt) {
        case 0:  return PixelFormat::kYUV420P;
        case 4:  return PixelFormat::kYUV422P;
        case 5:  return PixelFormat::kYUV444P;
        case 23: return PixelFormat::kNV12;
        case 26: return PixelFormat::kRGB32;
        default:
            if (av_pix_fmt == AV_PIX_FMT_D3D11)
                return PixelFormat::kD3D11;
            return PixelFormat::kUnknown;
    }
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

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        spdlog::error("SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    spdlog::info("VideoRenderer opened ({}x{})", width, height);
    return true;
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
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void VideoRenderer::Render(const MediaFrame& frame) {
    if (!renderer_ || !frame.IsValid()) return;

    switch (MapPixelFormat(frame.RawFrame()->format)) {
        case PixelFormat::kD3D11:
            RenderHWFrame(frame);
            break;
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
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_IYUV);
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
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_NV12);
    if (!texture_) return;

    SDL_UpdateNVTexture(texture_, nullptr,
                        av->data[0], av->linesize[0],
                        av->data[1], av->linesize[1]);
    Present(fw, fh);
}

void VideoRenderer::RenderHWFrame(const MediaFrame& frame) {
    AVFrame* hw_frame = frame.RawFrame();
    if (!hw_frame || !hw_frame->hw_frames_ctx) {
        RenderFallback(frame);
        return;
    }

    // 硬件帧 → 系统内存 NV12（DMA transfer，比软解快一个数量级）
    AVFramePtr sw_frame;
    sw_frame->format = AV_PIX_FMT_NV12;
    if (av_hwframe_transfer_data(sw_frame.get(), hw_frame, 0) < 0) {
        SPDLOG_WARN("VideoRenderer: hw frame transfer failed");
        RenderFallback(frame);
        return;
    }

    int fw = sw_frame->width;
    int fh = sw_frame->height;
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_NV12);
    if (!texture_) return;

    SDL_UpdateNVTexture(texture_, nullptr,
                        sw_frame->data[0], sw_frame->linesize[0],
                        sw_frame->data[1], sw_frame->linesize[1]);
    Present(fw, fh);
}

void VideoRenderer::RenderFallback(const MediaFrame& frame) {
    AVFrame* av = frame.RawFrame();
    int fw = av->width;
    int fh = av->height;
    EnsureTexture(fw, fh, SDL_PIXELFORMAT_IYUV);
    if (!texture_) return;

    AVFrame* src_frame = av;
    if (!src_frame) return;

    // 硬件帧需要先 transfer 到系统内存
    AVFrame* sw_frame = src_frame;
    AVFramePtr tmp_frame;
    if (src_frame->hw_frames_ctx) {
        if (av_hwframe_transfer_data(tmp_frame.get(), src_frame, 0) < 0) {
            SPDLOG_ERROR("VideoRenderer: hw frame transfer failed");
            return;
        }
        sw_frame = tmp_frame.get();
    }

    // 使用 sws_scale 转为 YUV420P
    if (!sws_ctx_) {
        sws_ctx_ = sws_getContext(
            fw, fh, static_cast<AVPixelFormat>(sw_frame->format),
            fw, fh, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    int needed = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, fw, fh, 1);
    if (needed > convert_buffer_size_) {
        if (convert_buffer_) av_free(convert_buffer_);
        convert_buffer_ = static_cast<uint8_t*>(av_malloc(needed));
        convert_buffer_size_ = needed;
    }

    uint8_t* dst_data[4] = {};
    int dst_linesize[4] = {};
    av_image_fill_arrays(dst_data, dst_linesize, convert_buffer_,
                         AV_PIX_FMT_YUV420P, fw, fh, 1);

    sws_scale(sws_ctx_, sw_frame->data, sw_frame->linesize, 0, fh,
              dst_data, dst_linesize);

    SDL_UpdateYUVTexture(texture_, nullptr,
                         dst_data[0], dst_linesize[0],
                         dst_data[1], dst_linesize[1],
                         dst_data[2], dst_linesize[2]);
    Present(fw, fh);
}

void VideoRenderer::Present(int frame_width, int frame_height) {
    RenderClear();
    float dx, dy, dw, dh;
    ComputeDestRect(frame_width, frame_height, &dx, &dy, &dw, &dh);
    SDL_FRect dst{dx, dy, dw, dh};
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

void VideoRenderer::Resize(int width, int height) {
    window_width_ = width;
    window_height_ = height;
}

void VideoRenderer::EnsureTexture(int frame_width, int frame_height, int sdl_format) {
    if (texture_ && texture_width_ == frame_width &&
        texture_height_ == frame_height && texture_format_ == sdl_format) {
        return;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
    }
    texture_ = SDL_CreateTexture(renderer_,
                                 static_cast<SDL_PixelFormat>(sdl_format),
                                 SDL_TEXTUREACCESS_STREAMING,
                                 frame_width, frame_height);
    if (!texture_) {
        spdlog::error("SDL_CreateTexture failed: {}", SDL_GetError());
        return;
    }
    texture_width_ = frame_width;
    texture_height_ = frame_height;
    texture_format_ = sdl_format;

    // Invalidate sws context on resolution/format change
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
}

void VideoRenderer::RenderClear() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
}

void VideoRenderer::ComputeDestRect(int frame_width, int frame_height,
                                     float* x, float* y, float* w, float* h) const {
    float src_aspect = static_cast<float>(frame_width) / static_cast<float>(frame_height);
    float dst_aspect = static_cast<float>(window_width_) / static_cast<float>(window_height_);

    float dst_w, dst_h;
    if (src_aspect > dst_aspect) {
        // Letterbox (bars top/bottom)
        dst_w = static_cast<float>(window_width_);
        dst_h = dst_w / src_aspect;
    } else {
        // Pillarbox (bars left/right)
        dst_h = static_cast<float>(window_height_);
        dst_w = dst_h * src_aspect;
    }

    *x = (static_cast<float>(window_width_) - dst_w) / 2.0f;
    *y = (static_cast<float>(window_height_) - dst_h) / 2.0f;
    *w = dst_w;
    *h = dst_h;
}

}  // namespace mvp
