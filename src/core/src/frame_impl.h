#ifndef MVP_FRAME_IMPL_H_
#define MVP_FRAME_IMPL_H_

// Internal header: defines Impl structs for VideoFrame/AudioFrame.
// Only to be included by frame_converter.cc and the respective .cc files.

extern "C" {
#include <libavutil/frame.h>
}

#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp {

struct VideoFrame::Impl {
    AVFrame* frame = nullptr;
    PixelFormat format = PixelFormat::kUnknown;
    double pts = 0.0;

    ~Impl() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

struct AudioFrame::Impl {
    AVFrame* frame = nullptr;
    SampleFormat format = SampleFormat::kUnknown;
    double pts = 0.0;

    ~Impl() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

}  // namespace mvp

#endif  // MVP_FRAME_IMPL_H_
