#include "frame_converter.h"

#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

#include "frame_impl.h"

namespace mvp {

static PixelFormat MapPixelFormat(int av_pix_fmt) {
    switch (av_pix_fmt) {
        case 0:  return PixelFormat::kYUV420P;    // AV_PIX_FMT_YUV420P
        case 4:  return PixelFormat::kYUV422P;    // AV_PIX_FMT_YUV422P
        case 5:  return PixelFormat::kYUV444P;    // AV_PIX_FMT_YUV444P
        case 23: return PixelFormat::kNV12;       // AV_PIX_FMT_NV12
        case 26: return PixelFormat::kRGB32;      // AV_PIX_FMT_RGB32 (actually BGRA on LE)
        default: return PixelFormat::kUnknown;
    }
}

static SampleFormat MapSampleFormat(int av_sample_fmt) {
    switch (av_sample_fmt) {
        case 1:  return SampleFormat::kS16;          // AV_SAMPLE_FMT_S16
        case 2:  return SampleFormat::kS32;          // AV_SAMPLE_FMT_S32
        case 3:  return SampleFormat::kFloat;        // AV_SAMPLE_FMT_FLT
        case 6:  return SampleFormat::kS16Planar;    // AV_SAMPLE_FMT_S16P
        case 8:  return SampleFormat::kFloatPlanar;  // AV_SAMPLE_FMT_FLTP
        default: return SampleFormat::kUnknown;
    }
}

VideoFrame FrameConverter::ToVideoFrame(AVFrame* src, AVStream* stream) {
    VideoFrame vf;
    auto impl = std::make_unique<VideoFrame::Impl>();

    impl->frame = av_frame_alloc();
    av_frame_ref(impl->frame, src);
    impl->format = MapPixelFormat(src->format);

    if (src->pts != AV_NOPTS_VALUE && stream) {
        impl->pts = static_cast<double>(src->pts) * av_q2d(stream->time_base);
    } else {
        impl->pts = 0.0;
    }

    vf.impl_ = std::move(impl);
    return vf;
}

AudioFrame FrameConverter::ToAudioFrame(AVFrame* src, AVStream* stream) {
    AudioFrame af;
    auto impl = std::make_unique<AudioFrame::Impl>();

    impl->frame = av_frame_alloc();
    av_frame_ref(impl->frame, src);
    impl->format = MapSampleFormat(src->format);

    if (src->pts != AV_NOPTS_VALUE && stream) {
        impl->pts = static_cast<double>(src->pts) * av_q2d(stream->time_base);
    } else {
        impl->pts = 0.0;
    }

    af.impl_ = std::move(impl);
    return af;
}

}  // namespace mvp
