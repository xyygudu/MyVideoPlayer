#ifndef MVP_FRAME_IMPL_H_
#define MVP_FRAME_IMPL_H_

// Internal header: defines Impl structs for VideoFrame/AudioFrame.
// Only to be included by frame_converter.cc, decoder, and the respective .cc files.

#include "ffmpeg_utils.h"
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

}  // namespace mvp

#endif  // MVP_FRAME_IMPL_H_
