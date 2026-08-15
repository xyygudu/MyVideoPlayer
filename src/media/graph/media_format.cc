#include "graph/media_format.h"

#include <algorithm>

namespace mvp::graph {

namespace {

/// Intersection of two value sets. An empty set means "any", so the
/// intersection with "any" is the other set.
template <typename T>
std::vector<T> IntersectVectors(const std::vector<T>& a,
                                const std::vector<T>& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::vector<T> result;
    for (const auto& x : a) {
        if (std::find(b.begin(), b.end(), x) != b.end()) result.push_back(x);
    }
    return result;
}

/// Both sides constrained with no overlap.
template <typename T>
bool Disjoint(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.empty() || b.empty()) return false;
    for (const auto& x : a) {
        if (std::find(b.begin(), b.end(), x) != b.end()) return false;
    }
    return true;
}

/// A zero max means "unbounded above".
bool RangesOverlap(int min_a, int max_a, int min_b, int max_b) {
    int lo = std::max(min_a, min_b);
    int hi = (max_a == 0) ? max_b : (max_b == 0) ? max_a : std::min(max_a, max_b);
    return hi == 0 || lo <= hi;
}

}  // namespace

// --- MediaFormat factory methods ---

MediaFormat MediaFormat::Video(int width, int height, PixelFormat fmt,
                              Rational frame_rate, PixelFormat hw_sw_format) {
    MediaFormat f;
    f.media_type_ = MediaType::kVideo;
    f.payload_ = VideoFormat{width, height, fmt, hw_sw_format, frame_rate};
    return f;
}

MediaFormat MediaFormat::Audio(int sample_rate, int channels,
                              SampleFormat fmt) {
    MediaFormat f;
    f.media_type_ = MediaType::kAudio;
    f.payload_ = AudioFormat{sample_rate, channels, fmt};
    return f;
}

MediaFormat MediaFormat::FromStream(int codec_id, Rational time_base,
                                    Rational frame_rate,
                                    const AVCodecParameters* codecpar,
                                    MediaType type) {
    MediaFormat f;
    f.media_type_ = type;
    f.time_base_ = time_base;

    EncodedFormat enc;
    enc.codec_id = codec_id;
    enc.frame_rate = frame_rate;
    if (codecpar) {
        // Deep copy codec parameters with custom deleter
        AVCodecParameters* p = avcodec_parameters_alloc();
        avcodec_parameters_copy(p, codecpar);
        enc.codec_params = std::shared_ptr<AVCodecParameters>(
            p, [](AVCodecParameters* x) { avcodec_parameters_free(&x); });
    }
    f.payload_ = std::move(enc);
    return f;
}

// --- FormatCaps ---

bool FormatCaps::Accepts(const MediaFormat& format) const {
    if (format.media_type() != media_type) {
        return false;
    }

    if (format.IsVideo()) {
        const auto& v = format.AsVideo();
        if (!pixel_formats.empty() &&
            std::find(pixel_formats.begin(), pixel_formats.end(),
                      v.pixel_format) == pixel_formats.end()) {
            return false;
        }
        if (max_width > 0 && (v.width < min_width || v.width > max_width)) {
            return false;
        }
        if (max_height > 0 && (v.height < min_height || v.height > max_height)) {
            return false;
        }
    }

    if (format.IsAudio()) {
        const auto& a = format.AsAudio();
        if (!sample_formats.empty() &&
            std::find(sample_formats.begin(), sample_formats.end(),
                      a.sample_format) == sample_formats.end()) {
            return false;
        }
        if (!sample_rates.empty() &&
            std::find(sample_rates.begin(), sample_rates.end(),
                      a.sample_rate) == sample_rates.end()) {
            return false;
        }
        if (!channel_counts.empty() &&
            std::find(channel_counts.begin(), channel_counts.end(),
                      a.channels) == channel_counts.end()) {
            return false;
        }
    }

    if (format.IsEncoded()) {
        const auto& e = format.AsEncoded();
        if (!codec_ids.empty() &&
            std::find(codec_ids.begin(), codec_ids.end(), e.codec_id) ==
                codec_ids.end()) {
            return false;
        }
    }

    return true;
}

bool FormatCaps::Compatible(const FormatCaps& a, const FormatCaps& b) {
    if (a.IsEmpty() || b.IsEmpty()) {
        return true;
    }
    if (a.media_type != b.media_type) {
        return false;
    }
    if (Disjoint(a.pixel_formats, b.pixel_formats) ||
        Disjoint(a.sample_formats, b.sample_formats) ||
        Disjoint(a.sample_rates, b.sample_rates) ||
        Disjoint(a.channel_counts, b.channel_counts) ||
        Disjoint(a.codec_ids, b.codec_ids)) {
        return false;
    }
    if (!RangesOverlap(a.min_width, a.max_width, b.min_width, b.max_width) ||
        !RangesOverlap(a.min_height, a.max_height, b.min_height, b.max_height)) {
        return false;
    }
    return a.header_placement == HeaderPlacement::kAny ||
           b.header_placement == HeaderPlacement::kAny ||
           a.header_placement == b.header_placement;
}

FormatCaps FormatCaps::Intersect(const FormatCaps& a, const FormatCaps& b) {
    if (a.media_type != b.media_type) {
        return {};  // Incompatible types
    }

    FormatCaps result;
    result.media_type = a.media_type;
    result.pixel_formats = IntersectVectors(a.pixel_formats, b.pixel_formats);
    result.min_width = std::max(a.min_width, b.min_width);
    result.max_width = (a.max_width == 0)   ? b.max_width
                       : (b.max_width == 0) ? a.max_width
                                            : std::min(a.max_width, b.max_width);
    result.min_height = std::max(a.min_height, b.min_height);
    result.max_height =
        (a.max_height == 0)   ? b.max_height
        : (b.max_height == 0) ? a.max_height
                              : std::min(a.max_height, b.max_height);
    result.sample_formats =
        IntersectVectors(a.sample_formats, b.sample_formats);
    result.sample_rates = IntersectVectors(a.sample_rates, b.sample_rates);
    result.channel_counts =
        IntersectVectors(a.channel_counts, b.channel_counts);
    result.codec_ids = IntersectVectors(a.codec_ids, b.codec_ids);
    result.header_placement = (a.header_placement == HeaderPlacement::kAny)
                                  ? b.header_placement
                                  : a.header_placement;
    return result;
}

bool FormatCaps::IsEmpty() const {
    // Empty if no formats specified and no range specified.
    // A caps with media_type but empty lists means "accepts any".
    // Truly empty means default-constructed (media_type is kUnknown).
    return media_type == MediaType{};
}

}  // namespace mvp::graph
