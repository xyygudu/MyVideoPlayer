// End-to-end transcodes of generated clips, checked with libavformat.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include "mvp/source_probe.h"
#include "mvp/transcoder.h"
#include "test_support.h"

namespace mvp {
namespace {

using namespace std::chrono_literals;

TranscodeOptions H264Aac() {
    TranscodeOptions options;
    options.video.codec_name = "libx264";
    options.video.preset = "ultrafast";
    options.video.gop_size = 50;
    options.audio.codec_name = "aac";
    options.audio.rate_control = RateControlMode::kBitrate;
    options.audio.bitrate_bps = 128000;
    return options;
}

// Runs one transcode to the end; kFailed if it did not finish in time.
TranscodeResult RunTranscode(const std::string& input, const std::string& output,
                             const TranscodeOptions& options) {
    Transcoder transcoder;
    std::promise<TranscodeResult> done;
    std::future<TranscodeResult> result = done.get_future();
    transcoder.SetInput(input);
    transcoder.SetOutput(output, options);
    transcoder.SetCompletionCallback([&done](TranscodeResult r) { done.set_value(r); });
    transcoder.Start();  // A failed start reports kFailed through the callback.
    if (result.wait_for(120s) != std::future_status::ready) {
        ADD_FAILURE() << "transcode of " << input << " did not finish";
        transcoder.Cancel();
        return TranscodeResult::kFailed;
    }
    return result.get();
}

// What a demuxer sees in one stream of a finished file.
struct StreamTimeline {
    int packets{0};
    bool dts_increasing{true};
    double end_seconds{0.0};  // Largest pts + duration
};

StreamTimeline ReadTimeline(const std::string& path, AVMediaType type) {
    StreamTimeline timeline;
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        ADD_FAILURE() << "cannot open " << path;
        return timeline;
    }
    avformat_find_stream_info(ctx, nullptr);
    const int index = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
    AVPacket* pkt = av_packet_alloc();
    int64_t last_dts = AV_NOPTS_VALUE;
    while (index >= 0 && av_read_frame(ctx, pkt) >= 0) {
        if (pkt->stream_index == index) {
            ++timeline.packets;
            if (last_dts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && pkt->dts <= last_dts) {
                timeline.dts_increasing = false;
            }
            last_dts = pkt->dts;
            if (pkt->pts != AV_NOPTS_VALUE) {
                const double end = (pkt->pts + pkt->duration) * av_q2d(ctx->streams[index]->time_base);
                timeline.end_seconds = std::max(timeline.end_seconds, end);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&ctx);
    return timeline;
}

TEST(TranscoderTest, ProducesACompleteFile) {
    const std::string output = test::ScratchPath("basic.mp4");
    ASSERT_EQ(RunTranscode(test::MediaPath("h264_aac_25fps.mp4"), output, H264Aac()),
              TranscodeResult::kSucceeded);

    const SourceInfo info = SourceProbe::Probe(output);
    ASSERT_EQ(info.video_streams.size(), 1u);
    ASSERT_EQ(info.audio_streams.size(), 1u);
    EXPECT_EQ(info.video_streams[0].width, 640);
    EXPECT_EQ(info.video_streams[0].height, 360);
    const StreamTimeline video = ReadTimeline(output, AVMEDIA_TYPE_VIDEO);
    EXPECT_EQ(video.packets, 150);  // 6 s at 25 fps: none lost, none duplicated
    EXPECT_NEAR(video.end_seconds, 6.0, 0.1);
}

// Opus only runs at 48 kHz: 44.1 kHz input must be resampled, not rejected.
TEST(TranscoderTest, ResamplesFor48kOnlyEncoders) {
    TranscodeOptions options = H264Aac();
    options.audio.codec_name = "libopus";
    options.audio.bitrate_bps = 96000;
    const std::string output = test::ScratchPath("opus.mkv");
    ASSERT_EQ(RunTranscode(test::MediaPath("h264_aac_25fps.mp4"), output, options),
              TranscodeResult::kSucceeded);

    const SourceInfo info = SourceProbe::Probe(output);
    ASSERT_EQ(info.audio_streams.size(), 1u);
    EXPECT_EQ(info.audio_streams[0].codec_name, "opus");
    EXPECT_EQ(info.audio_streams[0].sample_rate, 48000);
    EXPECT_NEAR(ReadTimeline(output, AVMEDIA_TYPE_AUDIO).end_seconds, 6.0, 0.15);
}

// Two TS segments that both start at the same timestamps, concatenated: the
// backward jump is folded so the output plays both, one after the other.
TEST(TranscoderTest, FoldsTransportStreamDiscontinuities) {
    const std::string output = test::ScratchPath("discont.mp4");
    ASSERT_EQ(RunTranscode(test::MediaPath("ts_discont.ts"), output, H264Aac()),
              TranscodeResult::kSucceeded);

    const StreamTimeline video = ReadTimeline(output, AVMEDIA_TYPE_VIDEO);
    EXPECT_TRUE(video.dts_increasing);
    EXPECT_GE(video.packets, 145);  // 2 x 3 s at 25 fps
    EXPECT_NEAR(video.end_seconds, 6.0, 0.2);
    const StreamTimeline audio = ReadTimeline(output, AVMEDIA_TYPE_AUDIO);
    EXPECT_TRUE(audio.dts_increasing);
    EXPECT_NEAR(audio.end_seconds, 6.0, 0.25);
}

// Resolution, sample rate and channel count change half-way: the encoders
// keep their negotiated format and the converters follow the input.
TEST(TranscoderTest, AdaptsToMidStreamFormatChanges) {
    const std::string output = test::ScratchPath("format_change.mp4");
    ASSERT_EQ(RunTranscode(test::MediaPath("ts_format_change.ts"), output, H264Aac()),
              TranscodeResult::kSucceeded);

    const SourceInfo info = SourceProbe::Probe(output);
    ASSERT_EQ(info.video_streams.size(), 1u);
    ASSERT_EQ(info.audio_streams.size(), 1u);
    EXPECT_EQ(info.video_streams[0].width, 640);
    EXPECT_EQ(info.video_streams[0].height, 360);
    EXPECT_EQ(info.audio_streams[0].sample_rate, 44100);
    EXPECT_EQ(info.audio_streams[0].channels, 2);
    const StreamTimeline video = ReadTimeline(output, AVMEDIA_TYPE_VIDEO);
    EXPECT_TRUE(video.dts_increasing);
    EXPECT_GE(video.packets, 148) << "at most the frames repeated at the splice may go";
    EXPECT_NEAR(video.end_seconds, 6.0, 0.2);
    EXPECT_NEAR(ReadTimeline(output, AVMEDIA_TYPE_AUDIO).end_seconds, 6.0, 0.25);
}

TEST(TranscoderTest, CancelReportsCancelledAndFinalizesTheFile) {
    TranscodeOptions options = H264Aac();
    options.video.preset = "medium";  // Slow enough to still be running.
    const std::string output = test::ScratchPath("cancelled.mp4");
    Transcoder transcoder;
    std::atomic<int> reports{0};
    std::atomic<TranscodeResult> last{TranscodeResult::kSucceeded};
    transcoder.SetInput(test::MediaPath("long_30s.mp4"));
    transcoder.SetOutput(output, options);
    transcoder.SetCompletionCallback([&](TranscodeResult r) {
        last = r;
        ++reports;
    });
    ASSERT_TRUE(transcoder.Start());
    EXPECT_FALSE(transcoder.Start()) << "a second run must not start while one is active";
    std::this_thread::sleep_for(500ms);
    transcoder.Cancel();

    EXPECT_EQ(reports.load(), 1);
    EXPECT_EQ(last.load(), TranscodeResult::kCancelled);
    const SourceInfo info = SourceProbe::Probe(output);  // Readable: the trailer was written.
    EXPECT_EQ(info.video_streams.size(), 1u);
    const StreamTimeline video = ReadTimeline(output, AVMEDIA_TYPE_VIDEO);
    EXPECT_GT(video.packets, 0);
    EXPECT_LT(video.end_seconds, 29.0);
}

TEST(TranscoderTest, MissingInputFailsThroughTheCallback) {
    Transcoder transcoder;
    std::atomic<int> reports{0};
    std::atomic<TranscodeResult> last{TranscodeResult::kSucceeded};
    transcoder.SetInput(test::MediaPath("does_not_exist.mp4"));
    transcoder.SetOutput(test::ScratchPath("never.mp4"), H264Aac());
    transcoder.SetCompletionCallback([&](TranscodeResult r) {
        last = r;
        ++reports;
    });
    EXPECT_FALSE(transcoder.Start());
    EXPECT_EQ(reports.load(), 1);
    EXPECT_EQ(last.load(), TranscodeResult::kFailed);
}

}  // namespace
}  // namespace mvp
