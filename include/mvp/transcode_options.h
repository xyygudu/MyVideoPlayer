#ifndef MVP_TRANSCODE_OPTIONS_H_
#define MVP_TRANSCODE_OPTIONS_H_

#include <cstdint>
#include <string>

namespace mvp {

/// Encoder rate-control strategy.
enum class RateControlMode {
    kCrf,      // Constant quality (encoder decides bitrate)
    kBitrate,  // Target average bitrate
};

/// Encoding parameters for a single elementary stream (video or audio).
/// An empty `codec_name` means "do not produce this media type" — the
/// Transcoder skips creating a Decoder/Encoder branch for it entirely.
struct EncodeParams {
    std::string codec_name;  // e.g. "libx264", "aac"; empty = no output stream
    RateControlMode rate_control{RateControlMode::kCrf};
    int crf{23};
    int64_t bitrate_bps{0};  // used when rate_control == kBitrate
    int gop_size{250};
    int max_b_frames{2};
    std::string preset{"medium"};  // ignored by encoders without a preset option
};

/// Full transcode configuration passed to Transcoder::SetOutput().
struct TranscodeOptions {
    EncodeParams video;  // video.codec_name empty => no video output
    EncodeParams audio;  // audio.codec_name empty => no audio output
};

}  // namespace mvp

#endif  // MVP_TRANSCODE_OPTIONS_H_
