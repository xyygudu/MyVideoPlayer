#include "graph/media_format.h"

#include <algorithm>

namespace mvp::graph {

// --- MediaFormat factory methods ---

MediaFormat MediaFormat::Video(int width, int height, PixelFormat fmt,
                              Rational frame_rate) {
    MediaFormat f;
    f.media_type_ = MediaType::kVideo;
    f.width_ = width;
    f.height_ = height;
    f.pixel_format_ = fmt;
    f.frame_rate_ = frame_rate;
    return f;
}

MediaFormat MediaFormat::Audio(int sample_rate, int channels,
                              SampleFormat fmt) {
    MediaFormat f;
    f.media_type_ = MediaType::kAudio;
    f.sample_rate_ = sample_rate;
    f.channels_ = channels;
    f.sample_format_ = fmt;
    return f;
}

MediaFormat MediaFormat::Packet(int codec_id, Rational time_base) {
    MediaFormat f;
    f.media_type_ = MediaType::kUnknown;  // Packet type
    f.codec_id_ = codec_id;
    f.time_base_ = time_base;
    return f;
}

MediaFormat MediaFormat::FromStream(int codec_id, Rational time_base,
                                    Rational frame_rate,
                                    const AVCodecParameters* codecpar,
                                    MediaType type) {
    MediaFormat f;
    f.media_type_ = type;
    f.codec_id_ = codec_id;
    f.time_base_ = time_base;
    f.frame_rate_ = frame_rate;

    if (codecpar) {
        // Deep copy codec parameters with custom deleter
        AVCodecParameters* p = avcodec_parameters_alloc();
        avcodec_parameters_copy(p, codecpar);
        f.codec_params_ = std::shared_ptr<AVCodecParameters>(
            p, [](AVCodecParameters* x) { avcodec_parameters_free(&x); });

        // Also fill convenience fields from codecpar
        if (type == MediaType::kVideo) {
            f.width_ = codecpar->width;
            f.height_ = codecpar->height;
        } else if (type == MediaType::kAudio) {
            f.sample_rate_ = codecpar->sample_rate;
            f.channels_ = codecpar->ch_layout.nb_channels;
        }
    }
    return f;
}

bool MediaFormat::IsVideo() const {
    return media_type_ == MediaType::kVideo;
}

bool MediaFormat::IsAudio() const {
    return media_type_ == MediaType::kAudio;
}

bool MediaFormat::IsPacket() const {
    return codec_id_ != 0;
}

// --- FormatCaps ---

bool FormatCaps::Accepts(const MediaFormat& format) const {
    if (format.media_type() != media_type) {
        return false;
    }

    if (format.IsVideo()) {
        if (!pixel_formats.empty()) {
            auto it = std::find(pixel_formats.begin(), pixel_formats.end(),
                                format.pixel_format());
            if (it == pixel_formats.end()) return false;
        }
        if (max_width > 0 &&
            (format.width() < min_width || format.width() > max_width)) {
            return false;
        }
        if (max_height > 0 &&
            (format.height() < min_height || format.height() > max_height)) {
            return false;
        }
    }

    if (format.IsAudio()) {
        if (!sample_formats.empty()) {
            auto it = std::find(sample_formats.begin(), sample_formats.end(),
                                format.sample_format());
            if (it == sample_formats.end()) return false;
        }
        if (!sample_rates.empty()) {
            auto it = std::find(sample_rates.begin(), sample_rates.end(),
                                format.sample_rate());
            if (it == sample_rates.end()) return false;
        }
        if (!channel_counts.empty()) {
            auto it = std::find(channel_counts.begin(), channel_counts.end(),
                                format.channels());
            if (it == channel_counts.end()) return false;
        }
    }

    if (format.IsPacket()) {
        if (!codec_ids.empty()) {
            auto it = std::find(codec_ids.begin(), codec_ids.end(),
                                format.codec_id());
            if (it == codec_ids.end()) return false;
        }
    }

    return true;
}

FormatCaps FormatCaps::Intersect(const FormatCaps& a, const FormatCaps& b) {
    if (a.media_type != b.media_type) {
        return {};  // Incompatible types
    }

    FormatCaps result;
    result.media_type = a.media_type;

    // Pixel formats intersection
    if (a.pixel_formats.empty()) {
        result.pixel_formats = b.pixel_formats;
    } else if (b.pixel_formats.empty()) {
        result.pixel_formats = a.pixel_formats;
    } else {
        for (auto fmt : a.pixel_formats) {
            if (std::find(b.pixel_formats.begin(), b.pixel_formats.end(),
                          fmt) != b.pixel_formats.end()) {
                result.pixel_formats.push_back(fmt);
            }
        }
    }

    // Resolution: take tighter bounds
    result.min_width = std::max(a.min_width, b.min_width);
    result.max_width = (a.max_width == 0)   ? b.max_width
                       : (b.max_width == 0) ? a.max_width
                                            : std::min(a.max_width, b.max_width);
    result.min_height = std::max(a.min_height, b.min_height);
    result.max_height =
        (a.max_height == 0)   ? b.max_height
        : (b.max_height == 0) ? a.max_height
                              : std::min(a.max_height, b.max_height);

    // Sample formats intersection
    if (a.sample_formats.empty()) {
        result.sample_formats = b.sample_formats;
    } else if (b.sample_formats.empty()) {
        result.sample_formats = a.sample_formats;
    } else {
        for (auto fmt : a.sample_formats) {
            if (std::find(b.sample_formats.begin(), b.sample_formats.end(),
                          fmt) != b.sample_formats.end()) {
                result.sample_formats.push_back(fmt);
            }
        }
    }

    // Sample rates intersection
    if (a.sample_rates.empty()) {
        result.sample_rates = b.sample_rates;
    } else if (b.sample_rates.empty()) {
        result.sample_rates = a.sample_rates;
    } else {
        for (auto rate : a.sample_rates) {
            if (std::find(b.sample_rates.begin(), b.sample_rates.end(),
                          rate) != b.sample_rates.end()) {
                result.sample_rates.push_back(rate);
            }
        }
    }

    // Channel counts intersection
    if (a.channel_counts.empty()) {
        result.channel_counts = b.channel_counts;
    } else if (b.channel_counts.empty()) {
        result.channel_counts = a.channel_counts;
    } else {
        for (auto ch : a.channel_counts) {
            if (std::find(b.channel_counts.begin(), b.channel_counts.end(),
                          ch) != b.channel_counts.end()) {
                result.channel_counts.push_back(ch);
            }
        }
    }

    // Codec IDs intersection
    if (a.codec_ids.empty()) {
        result.codec_ids = b.codec_ids;
    } else if (b.codec_ids.empty()) {
        result.codec_ids = a.codec_ids;
    } else {
        for (auto id : a.codec_ids) {
            if (std::find(b.codec_ids.begin(), b.codec_ids.end(), id) !=
                b.codec_ids.end()) {
                result.codec_ids.push_back(id);
            }
        }
    }

    return result;
}

bool FormatCaps::IsEmpty() const {
    // Empty if no formats specified and no range specified.
    // A caps with media_type but empty lists means "accepts any".
    // Truly empty means default-constructed (media_type is kUnknown).
    return media_type == MediaType{};
}

}  // namespace mvp::graph
