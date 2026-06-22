#ifndef MVP_GRAPH_MEDIA_FORMAT_H_
#define MVP_GRAPH_MEDIA_FORMAT_H_

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/codec_par.h>
}

#include "graph/media_buffer.h"
#include "media_frame.h"
#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp::graph {

/// Describes the negotiated format on a port connection.
/// Uses project-local enums — no FFmpeg types exposed.
class MediaFormat {
  public:
    MediaFormat() = default;

    // --- Video format ---
    static MediaFormat Video(int width, int height, PixelFormat fmt,
                            Rational frame_rate = {});

    // --- Audio format ---
    static MediaFormat Audio(int sample_rate, int channels,
                            SampleFormat fmt);

    // --- Packet (compressed) format ---
    static MediaFormat Packet(int codec_id, Rational time_base);

    // --- Full stream format (from DemuxNode, carries codec params copy) ---
    static MediaFormat FromStream(int codec_id, Rational time_base,
                                  Rational frame_rate,
                                  const AVCodecParameters* codecpar,
                                  MediaType type);

    MediaType media_type() const { return media_type_; }
    bool IsVideo() const;
    bool IsAudio() const;
    bool IsPacket() const;

    // Video accessors
    int width() const { return width_; }
    int height() const { return height_; }
    PixelFormat pixel_format() const { return pixel_format_; }
    Rational frame_rate() const { return frame_rate_; }

    // Audio accessors
    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    SampleFormat sample_format() const { return sample_format_; }

    // Packet accessors
    int codec_id() const { return codec_id_; }
    Rational time_base() const { return time_base_; }

    // Codec parameters (shared copy, may be null for raw frame formats)
    const AVCodecParameters* codec_params() const { return codec_params_.get(); }

  private:
    MediaType media_type_{};
    // Video
    int width_{0};
    int height_{0};
    PixelFormat pixel_format_{};
    Rational frame_rate_;
    // Audio
    int sample_rate_{0};
    int channels_{0};
    SampleFormat sample_format_{};
    // Packet
    int codec_id_{0};
    Rational time_base_;
    // Codec parameters (deep copy, shared ownership for cheap MediaFormat copies)
    std::shared_ptr<AVCodecParameters> codec_params_;
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
