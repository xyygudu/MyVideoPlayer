#ifndef MVP_FRAME_CONVERTER_H_
#define MVP_FRAME_CONVERTER_H_

#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

struct AVFrame;
struct AVStream;

namespace mvp {

// Internal utility: converts raw AVFrame to public VideoFrame/AudioFrame types.
// These functions create a ref-counted copy (av_frame_ref), so the source
// AVFrame can be safely unref'd after conversion.
class FrameConverter {
  public:
    // Convert a decoded video AVFrame to a VideoFrame.
    // The AVStream is used to compute PTS in seconds.
    static VideoFrame ToVideoFrame(AVFrame* src, AVStream* stream);

    // Convert a decoded audio AVFrame to an AudioFrame.
    static AudioFrame ToAudioFrame(AVFrame* src, AVStream* stream);
};

}  // namespace mvp

#endif  // MVP_FRAME_CONVERTER_H_
