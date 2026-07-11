#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mvp {

/// Describes a single video stream discovered during source probing.
struct VideoStream {
    int index{-1};                       ///< Stream index (av_read_frame routing)
    std::string codec_name;              ///< Codec name string (e.g. "h264", "hevc")
    int width{0};
    int height{0};
    int frame_rate_num{0};               ///< Frame rate numerator
    int frame_rate_den{1};               ///< Frame rate denominator
    int pix_fmt{-1};                     ///< AVPixelFormat value, -1 = unknown
    int64_t bit_rate{0};
};

/// Describes a single audio stream discovered during source probing.
struct AudioStream {
    int index{-1};                       ///< Stream index (av_read_frame routing)
    std::string codec_name;              ///< Codec name string (e.g. "aac", "mp3")
    int sample_rate{0};
    int channels{0};
    uint64_t channel_layout{0};
    int sample_fmt{-1};                  ///< AVSampleFormat value, -1 = unknown
    int64_t bit_rate{0};
};

/// Container-level information about a media source, plus all streams.
///
/// Designed to be reusable across UI display, stream selection, CanPlay()
/// pre-flight checks, and graph construction.
struct SourceInfo {
    std::string filepath;
    double duration{0.0};                ///< Duration in seconds
    std::string format_name;             ///< Container format (e.g. "mov,mp4,m4a")
    int64_t bit_rate{0};                 ///< Overall bit rate, 0 = unknown
    std::vector<VideoStream> video_streams;
    std::vector<AudioStream> audio_streams;
};

}  // namespace mvp
