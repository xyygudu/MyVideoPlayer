#ifndef MVP_FRAME_IMPL_H_
#define MVP_FRAME_IMPL_H_

// Internal header: defines Impl structs for VideoFrame/AudioFrame,
// format mapping utilities, and factory functions from MediaFrame.
// Only to be included by core/src internal .cc files.

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "ffmpeg_utils.h"
#include "media_frame.h"
#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp {

struct VideoFrame::Impl {
    AVFramePtr frame;
    PixelFormat format = PixelFormat::kUnknown;
    double pts = 0.0;
};

struct AudioFrame::Impl {
    AVFramePtr frame;
    SampleFormat format = SampleFormat::kUnknown;
    double pts = 0.0;
};

// 内部模块获取底层 AVFrame 的统一入口。
// 仅限 core/src 内部使用，不暴露在公共头文件中。
inline AVFrame* GetInternalFrame(const VideoFrame& f) {
    return f.impl_ ? f.impl_->frame.get() : nullptr;
}

inline AVFrame* GetInternalFrame(const AudioFrame& f) {
    return f.impl_ ? f.impl_->frame.get() : nullptr;
}

// --- Pixel/Sample format mapping ---

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

inline SampleFormat MapSampleFormat(int av_sample_fmt) {
    switch (av_sample_fmt) {
        case 1:  return SampleFormat::kS16;
        case 2:  return SampleFormat::kS32;
        case 3:  return SampleFormat::kFloat;
        case 6:  return SampleFormat::kS16Planar;
        case 8:  return SampleFormat::kFloatPlanar;
        default: return SampleFormat::kUnknown;
    }
}

// --- Factory functions: MediaFrame → public frame types ---

inline VideoFrame MakeVideoFrame(const MediaFrame& mf) {
    VideoFrame vf;
    auto impl = std::make_unique<VideoFrame::Impl>();
    av_frame_ref(impl->frame.get(), mf.RawFrame());
    impl->format = MapPixelFormat(mf.RawFrame()->format);
    impl->pts = mf.pts();
    vf.impl_ = std::move(impl);
    return vf;
}

inline AudioFrame MakeAudioFrame(const MediaFrame& mf) {
    AudioFrame af;
    auto impl = std::make_unique<AudioFrame::Impl>();
    av_frame_ref(impl->frame.get(), mf.RawFrame());
    impl->format = MapSampleFormat(mf.RawFrame()->format);
    impl->pts = mf.pts();
    af.impl_ = std::move(impl);
    return af;
}

}  // namespace mvp

#endif  // MVP_FRAME_IMPL_H_
