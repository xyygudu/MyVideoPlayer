#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include "audio_resampler.h"

namespace mvp {
namespace {

constexpr int kFrameSamples = 1024;

struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

FramePtr MakeSilence(int rate, const AVChannelLayout& layout, AVSampleFormat format, int samples) {
    FramePtr f(av_frame_alloc());
    f->format = format;
    f->sample_rate = rate;
    f->nb_samples = samples;
    av_channel_layout_copy(&f->ch_layout, &layout);
    EXPECT_GE(av_frame_get_buffer(f.get(), 0), 0);
    av_samples_set_silence(f->extended_data, 0, samples, layout.nb_channels, format);
    return f;
}

// Output frames used to be sized for the input sample count, so upsampling
// cut every frame short, and the samples swr buffers were never drained.
TEST(AudioResamplerTest, RateConversionKeepsEverySample) {
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AudioResampler resampler(AV_SAMPLE_FMT_S16, 48000, stereo);
    FramePtr out(av_frame_alloc());
    constexpr int kFrames = 100;
    int64_t produced = 0;
    for (int i = 0; i < kFrames; ++i) {
        FramePtr in = MakeSilence(44100, stereo, AV_SAMPLE_FMT_FLTP, kFrameSamples);
        ASSERT_TRUE(resampler.Convert(in.get(), out.get()));
        produced += out->nb_samples;
    }
    ASSERT_TRUE(resampler.Convert(nullptr, out.get()));  // End of stream: drain.
    produced += out->nb_samples;

    const double expected = kFrames * kFrameSamples * 48000.0 / 44100.0;
    EXPECT_NEAR(static_cast<double>(produced), expected, 4.0);
}

// A stream may switch rate, layout and sample format mid-way (TS, HLS); the
// output format must not change with it.
TEST(AudioResamplerTest, FollowsInputChangesWithAFixedOutput) {
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    const AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    AudioResampler resampler(AV_SAMPLE_FMT_FLTP, 48000, stereo);
    FramePtr out(av_frame_alloc());

    FramePtr first = MakeSilence(44100, stereo, AV_SAMPLE_FMT_FLTP, kFrameSamples);
    ASSERT_TRUE(resampler.Convert(first.get(), out.get()));
    FramePtr second = MakeSilence(32000, mono, AV_SAMPLE_FMT_S16, kFrameSamples);
    ASSERT_TRUE(resampler.Convert(second.get(), out.get()));

    EXPECT_EQ(out->format, AV_SAMPLE_FMT_FLTP);
    EXPECT_EQ(out->sample_rate, 48000);
    EXPECT_EQ(out->ch_layout.nb_channels, 2);
    EXPECT_GT(out->nb_samples, 0);
}

TEST(AudioResamplerTest, DrainWithoutInputIsANoOp) {
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AudioResampler resampler(AV_SAMPLE_FMT_S16, 48000, stereo);
    FramePtr out(av_frame_alloc());
    EXPECT_TRUE(resampler.Convert(nullptr, out.get()));
    EXPECT_EQ(out->nb_samples, 0);
}

}  // namespace
}  // namespace mvp
