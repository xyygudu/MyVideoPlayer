// AudioMixTimeline: how the recorder's mixer places and mixes audio streams.
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "audio/audio_mix_timeline.h"

namespace mvp {
namespace {

constexpr int64_t kTolerance = 10;

// Writes `count` samples of `value` on both channels.
void WriteConstant(AudioMixTimeline& timeline, int input, int count, int64_t start, float value) {
    std::vector<float> left(static_cast<size_t>(count), value);
    std::vector<float> right(static_cast<size_t>(count), value);
    const float* planes[2] = {left.data(), right.data()};
    timeline.Write(input, planes, count, start);
}

// Mixes `count` samples; returns the left channel.
std::vector<float> Mix(AudioMixTimeline& timeline, int count, std::vector<float> gains,
                       std::vector<float>* peaks = nullptr) {
    std::vector<float> left(static_cast<size_t>(count));
    std::vector<float> right(static_cast<size_t>(count));
    float* out[2] = {left.data(), right.data()};
    std::vector<float> chunk_peaks(gains.size());
    timeline.MixNext(out, count, gains.data(), chunk_peaks.data());
    EXPECT_EQ(left, right);
    if (peaks) {
        *peaks = chunk_peaks;
    }
    return left;
}

TEST(AudioMixTimelineTest, TimestampJitterContinuesSeamlessly) {
    AudioMixTimeline timeline(1, 2, kTolerance);
    WriteConstant(timeline, 0, 100, 0, 0.5f);
    WriteConstant(timeline, 0, 100, 105, 0.25f);  // 5 samples "late": still contiguous
    EXPECT_EQ(timeline.BufferedEnd(0), 200);
    const std::vector<float> out = Mix(timeline, 200, {1.0f});
    EXPECT_FLOAT_EQ(out[99], 0.5f);
    EXPECT_FLOAT_EQ(out[100], 0.25f);
    EXPECT_FLOAT_EQ(out[199], 0.25f);
    EXPECT_EQ(timeline.ReadPosition(), 200);
}

TEST(AudioMixTimelineTest, GapsReadAsSilenceAndOverlapsAreDropped) {
    AudioMixTimeline timeline(1, 2, kTolerance);
    WriteConstant(timeline, 0, 50, 0, 0.5f);
    WriteConstant(timeline, 0, 50, 100, 0.25f);  // Gap 50..100
    WriteConstant(timeline, 0, 50, 120, 0.75f);  // Overlaps 120..150: only 150..170 is new
    EXPECT_EQ(timeline.BufferedEnd(0), 170);
    const std::vector<float> out = Mix(timeline, 170, {1.0f});
    EXPECT_FLOAT_EQ(out[49], 0.5f);
    EXPECT_FLOAT_EQ(out[50], 0.0f);
    EXPECT_FLOAT_EQ(out[99], 0.0f);
    EXPECT_FLOAT_EQ(out[100], 0.25f);
    EXPECT_FLOAT_EQ(out[149], 0.25f);
    EXPECT_FLOAT_EQ(out[150], 0.75f);
    EXPECT_FLOAT_EQ(out[169], 0.75f);
}

TEST(AudioMixTimelineTest, SamplesBeforeTheReadPositionAreTooLate) {
    AudioMixTimeline timeline(1, 2, kTolerance);
    Mix(timeline, 100, {1.0f});  // Nothing written yet: 100 samples of silence went out
    WriteConstant(timeline, 0, 50, 80, 0.5f);
    EXPECT_EQ(timeline.BufferedEnd(0), 130);
    const std::vector<float> out = Mix(timeline, 30, {1.0f});
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[29], 0.5f);
}

TEST(AudioMixTimelineTest, AppliesGainsReportsPeaksAndClips) {
    AudioMixTimeline timeline(2, 2, kTolerance);
    WriteConstant(timeline, 0, 10, 0, 0.5f);
    WriteConstant(timeline, 1, 10, 0, 0.75f);
    std::vector<float> peaks;
    std::vector<float> out = Mix(timeline, 10, {1.0f, 0.5f}, &peaks);
    EXPECT_FLOAT_EQ(out[0], 0.875f);
    EXPECT_FLOAT_EQ(peaks[0], 0.5f);
    EXPECT_FLOAT_EQ(peaks[1], 0.375f);

    WriteConstant(timeline, 0, 10, 10, 0.9f);
    WriteConstant(timeline, 1, 10, 10, -0.2f);
    out = Mix(timeline, 10, {2.0f, 1.0f});
    EXPECT_FLOAT_EQ(out[0], 1.0f);  // 1.8 - 0.2, clipped
}

TEST(AudioMixTimelineTest, AnInputWithoutDataIsSilent) {
    AudioMixTimeline timeline(2, 2, kTolerance);
    WriteConstant(timeline, 0, 20, 0, 0.5f);
    EXPECT_EQ(timeline.BufferedEnd(1), 0);
    std::vector<float> peaks;
    const std::vector<float> out = Mix(timeline, 20, {1.0f, 1.0f}, &peaks);
    EXPECT_FLOAT_EQ(out[10], 0.5f);
    EXPECT_FLOAT_EQ(peaks[1], 0.0f);
    EXPECT_EQ(timeline.BufferedEnd(1), 20);  // Follows the read position
}

}  // namespace
}  // namespace mvp
