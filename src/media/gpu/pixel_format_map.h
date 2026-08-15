#ifndef MVP_GPU_PIXEL_FORMAT_MAP_H_
#define MVP_GPU_PIXEL_FORMAT_MAP_H_

extern "C" {
#include <libavutil/pixfmt.h>
}

#include "media_frame.h"

namespace mvp::gpu {

/// FFmpeg AVPixelFormat → project PixelFormat. The single place where the two
/// format vocabularies meet; kUnknown for formats the enum does not model.
inline PixelFormat FromAvPixelFormat(int av_pix_fmt) {
    switch (av_pix_fmt) {
        case AV_PIX_FMT_YUV420P: return PixelFormat::kYUV420P;
        case AV_PIX_FMT_YUV422P: return PixelFormat::kYUV422P;
        case AV_PIX_FMT_YUV444P: return PixelFormat::kYUV444P;
        case AV_PIX_FMT_NV12:    return PixelFormat::kNV12;
        case AV_PIX_FMT_RGB32:   return PixelFormat::kRGB32;
        case AV_PIX_FMT_D3D11:   return PixelFormat::kD3D11;
        case AV_PIX_FMT_CUDA:    return PixelFormat::kCuda;
        case AV_PIX_FMT_QSV:     return PixelFormat::kQsv;
        case AV_PIX_FMT_VAAPI:   return PixelFormat::kVAAPI;
        case AV_PIX_FMT_VIDEOTOOLBOX: return PixelFormat::kVideoToolbox;
        default: return PixelFormat::kUnknown;
    }
}

/// Project PixelFormat → FFmpeg AVPixelFormat; AV_PIX_FMT_NONE for kUnknown.
inline AVPixelFormat ToAvPixelFormat(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::kYUV420P: return AV_PIX_FMT_YUV420P;
        case PixelFormat::kYUV422P: return AV_PIX_FMT_YUV422P;
        case PixelFormat::kYUV444P: return AV_PIX_FMT_YUV444P;
        case PixelFormat::kNV12:    return AV_PIX_FMT_NV12;
        case PixelFormat::kRGB32:   return AV_PIX_FMT_RGB32;
        case PixelFormat::kD3D11:   return AV_PIX_FMT_D3D11;
        case PixelFormat::kCuda:    return AV_PIX_FMT_CUDA;
        case PixelFormat::kQsv:     return AV_PIX_FMT_QSV;
        case PixelFormat::kVAAPI:   return AV_PIX_FMT_VAAPI;
        case PixelFormat::kVideoToolbox: return AV_PIX_FMT_VIDEOTOOLBOX;
        default: return AV_PIX_FMT_NONE;
    }
}

}  // namespace mvp::gpu

#endif  // MVP_GPU_PIXEL_FORMAT_MAP_H_
