#include "audio/audio_mix_timeline.h"

#include <algorithm>
#include <cmath>

namespace mvp {

AudioMixTimeline::AudioMixTimeline(int inputs, int channels, int64_t tolerance)
    : channels_(channels), tolerance_(tolerance), tracks_(static_cast<size_t>(inputs)) {
    for (Track& track : tracks_) {
        track.channels.resize(static_cast<size_t>(channels));
    }
}

void AudioMixTimeline::Write(int input, const float* const* planes, int count, int64_t start) {
    Track& track = tracks_[static_cast<size_t>(input)];
    int64_t skip = 0;
    if (track.end != kNoEnd) {
        if (std::llabs(start - track.end) <= tolerance_) {
            start = track.end;  // Jitter: continue seamlessly
        } else if (start < track.end) {
            skip = track.end - start;  // Overlap: keep what is already there
        }
    }
    skip = std::max(skip, read_position_ - start);  // Too late for these
    if (skip >= count) {
        track.end = std::max(track.end, start + count);
        return;
    }
    const int64_t first = start + skip;
    const auto offset = static_cast<size_t>(first - read_position_);
    const auto kept = static_cast<size_t>(count - skip);
    for (int c = 0; c < channels_; ++c) {
        std::vector<float>& samples = track.channels[static_cast<size_t>(c)];
        if (samples.size() < offset + kept) {
            samples.resize(offset + kept, 0.0f);  // A gap before `first` reads as silence
        }
        std::copy_n(planes[c] + skip, kept, samples.begin() + static_cast<ptrdiff_t>(offset));
    }
    track.end = first + static_cast<int64_t>(kept);
}

int64_t AudioMixTimeline::BufferedEnd(int input) const {
    const Track& track = tracks_[static_cast<size_t>(input)];
    return std::max(track.end, read_position_);
}

void AudioMixTimeline::MixNext(float* const* out, int count, const float* gains, float* peaks) {
    for (int c = 0; c < channels_; ++c) {
        std::fill_n(out[c], count, 0.0f);
    }
    for (size_t i = 0; i < tracks_.size(); ++i) {
        float peak = 0.0f;
        for (int c = 0; c < channels_; ++c) {
            std::vector<float>& samples = tracks_[i].channels[static_cast<size_t>(c)];
            const auto available = static_cast<int>(std::min<size_t>(samples.size(), static_cast<size_t>(count)));
            for (int s = 0; s < available; ++s) {
                const float value = samples[static_cast<size_t>(s)] * gains[i];
                out[c][s] += value;
                peak = std::max(peak, std::fabs(value));
            }
            samples.erase(samples.begin(), samples.begin() + available);
        }
        peaks[i] = peak;
    }
    for (int c = 0; c < channels_; ++c) {
        std::for_each(out[c], out[c] + count, [](float& v) { v = std::clamp(v, -1.0f, 1.0f); });
    }
    read_position_ += count;
}

}  // namespace mvp
