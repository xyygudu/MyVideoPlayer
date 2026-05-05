#include "stream_context.h"

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "ffmpeg_utils.h"
#include "frame_impl.h"
#include "hw_accel_context.h"
#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp {

// --- Pixel/Sample format mapping (migrated from frame_converter.cc) ---

static PixelFormat MapPixelFormat(int av_pix_fmt) {
    switch (av_pix_fmt) {
        case 0:  return PixelFormat::kYUV420P;
        case 4:  return PixelFormat::kYUV422P;
        case 5:  return PixelFormat::kYUV444P;
        case 23: return PixelFormat::kNV12;
        case 26: return PixelFormat::kRGB32;
        default:
            // D3D11VA 硬件帧格式
            if (av_pix_fmt == AV_PIX_FMT_D3D11)
                return PixelFormat::kD3D11;
            return PixelFormat::kUnknown;
    }
}

static SampleFormat MapSampleFormat(int av_sample_fmt) {
    switch (av_sample_fmt) {
        case 1:  return SampleFormat::kS16;
        case 2:  return SampleFormat::kS32;
        case 3:  return SampleFormat::kFloat;
        case 6:  return SampleFormat::kS16Planar;
        case 8:  return SampleFormat::kFloatPlanar;
        default: return SampleFormat::kUnknown;
    }
}

// --- StreamContext<VideoFrame> ---

template<>
StreamContext<VideoFrame>::StreamContext(int frame_queue_size, int64_t max_packet_bytes)
    : packet_queue(max_packet_bytes), frame_queue(frame_queue_size) {}

template<>
bool StreamContext<VideoFrame>::OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx) {
    return decoder.Open(stream, hw_ctx);
}

template<>
void StreamContext<VideoFrame>::Start() {
    // StreamContext 作为管线管理者，负责将 Decoder 输出的原始 AVFrame 封装为公共帧类型。
    // 这里的 lambda 就是 Decoder 的 FrameOutputCallback：接收原始数据，构建 VideoFrame 并入队。
    auto on_frame = [this](AVFrame* raw, double pts, int serial) {
        VideoFrame vf;
        auto impl = std::make_unique<VideoFrame::Impl>();
        av_frame_ref(impl->frame.get(), raw);
        impl->format = MapPixelFormat(raw->format);
        impl->pts = pts;
        vf.impl_ = std::move(impl);
        frame_queue.Push(QueueEntry<VideoFrame>{std::move(vf), serial, false});
    };
    auto on_eof = [this](int serial) {
        frame_queue.PushEof(serial);
    };
    decoder.Start(&packet_queue, std::move(on_frame), std::move(on_eof));
}

template<>
void StreamContext<VideoFrame>::Stop() { decoder.Stop(); }

template<>
void StreamContext<VideoFrame>::Flush() {
    packet_queue.Flush();
    frame_queue.Flush();
}

template<>
void StreamContext<VideoFrame>::Abort() {
    packet_queue.Abort();
    frame_queue.Abort();
    decoder.Stop();
}

// --- StreamContext<AudioFrame> ---

template<>
StreamContext<AudioFrame>::StreamContext(int frame_queue_size, int64_t max_packet_bytes)
    : packet_queue(max_packet_bytes), frame_queue(frame_queue_size) {}

template<>
bool StreamContext<AudioFrame>::OpenDecoder(AVStream* stream, HWAccelContext* /*hw_ctx*/) {
    return decoder.Open(stream);
}

template<>
void StreamContext<AudioFrame>::Start() {
    // 同 VideoFrame 的 Start()，将原始 AVFrame 封装为 AudioFrame 并入队。
    auto on_frame = [this](AVFrame* raw, double pts, int serial) {
        AudioFrame af;
        auto impl = std::make_unique<AudioFrame::Impl>();
        av_frame_ref(impl->frame.get(), raw);
        impl->format = MapSampleFormat(raw->format);
        impl->pts = pts;
        af.impl_ = std::move(impl);
        frame_queue.Push(QueueEntry<AudioFrame>{std::move(af), serial, false});
    };
    auto on_eof = [this](int serial) {
        frame_queue.PushEof(serial);
    };
    decoder.Start(&packet_queue, std::move(on_frame), std::move(on_eof));
}

template<>
void StreamContext<AudioFrame>::Stop() { decoder.Stop(); }

template<>
void StreamContext<AudioFrame>::Flush() {
    packet_queue.Flush();
    frame_queue.Flush();
}

template<>
void StreamContext<AudioFrame>::Abort() {
    packet_queue.Abort();
    frame_queue.Abort();
    decoder.Stop();
}

}  // namespace mvp
