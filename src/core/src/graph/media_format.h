#ifndef MVP_GRAPH_MEDIA_FORMAT_H_
#define MVP_GRAPH_MEDIA_FORMAT_H_

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <variant>
#include <vector>

extern "C" {
#include <libavcodec/codec_par.h>
}

#include "graph/media_buffer.h"
#include "media_frame.h"
#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp::graph {

/// Compressed stream format (Demux -> Decoder link).
/// Carries the full codec parameters needed to open a decoder.
struct EncodedFormat {
    int codec_id{0};
    Rational frame_rate;  // Nominal video fps (0/1 for audio); not in codecpar
    std::shared_ptr<AVCodecParameters> codec_params;  // Deep copy, shared
};

/// Decoded video frame format (Decoder -> Sink/Filter link).
struct VideoFormat {
    int width{0};
    int height{0};
    PixelFormat pixel_format{};
    Rational frame_rate;
};

/// Decoded audio frame format (Decoder -> Sink/Filter link).
struct AudioFormat {
    int sample_rate{0};
    int channels{0};
    SampleFormat sample_format{};
};

/// Describes the negotiated format on a port connection.
///
/// Common fields (media_type, time_base) live outside; type-specific payload
/// is a closed-set variant. Encoded vs decoded are distinct alternatives, so
/// codec_params only exists on EncodedFormat (no fat-struct null slots).
class MediaFormat {
  public:
    MediaFormat() = default;

    // --- Factories ---
    static MediaFormat Video(int width, int height, PixelFormat fmt,
                             Rational frame_rate = {});
    static MediaFormat Audio(int sample_rate, int channels, SampleFormat fmt);
    /// Compressed stream format from a demuxer (deep-copies codecpar).
    static MediaFormat FromStream(int codec_id, Rational time_base,
                                  Rational frame_rate,
                                  const AVCodecParameters* codecpar,
                                  MediaType type);

    // --- Common fields ---
    MediaType media_type() const { return media_type_; }
    Rational time_base() const { return time_base_; }

    // --- Type-safe variant access ---
    bool IsEncoded() const {
        return std::holds_alternative<EncodedFormat>(payload_);
    }
    bool IsVideo() const {
        return std::holds_alternative<VideoFormat>(payload_);
    }
    bool IsAudio() const {
        return std::holds_alternative<AudioFormat>(payload_);
    }

    const EncodedFormat& AsEncoded() const {
        return std::get<EncodedFormat>(payload_);
    }
    const VideoFormat& AsVideo() const {
        return std::get<VideoFormat>(payload_);
    }
    const AudioFormat& AsAudio() const {
        return std::get<AudioFormat>(payload_);
    }

  private:
    MediaType media_type_{};
    Rational time_base_;
    std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat>
        payload_;
};

/// Describes the range of formats a port can accept or produce.
/// Used during Connect() for compatibility checking.
struct FormatCaps {
    MediaType media_type{};

    // Video capabilities (empty = any)
    std::vector<PixelFormat> pixel_formats;
    int min_width{0};
    int max_width{0};
    int min_height{0};
    int max_height{0};

    // Audio capabilities (empty = any)
    std::vector<SampleFormat> sample_formats;
    std::vector<int> sample_rates;       // Empty = any
    std::vector<int> channel_counts;     // Empty = any

    // Packet capabilities
    std::vector<int> codec_ids;          // Empty = any

    /// Check if a specific MediaFormat is compatible with these caps.
    bool Accepts(const MediaFormat& format) const;

    /// Compute intersection of two FormatCaps. Returns empty caps if
    /// no overlap.
    static FormatCaps Intersect(const FormatCaps& a, const FormatCaps& b);

    bool IsEmpty() const;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MEDIA_FORMAT_H_
