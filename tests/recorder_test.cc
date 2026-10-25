// Screen recording end to end on this machine's real desktop and devices:
// device lists, the recorder graph and the files it writes (read back with
// libavformat). Device-dependent cases skip when the device is missing.
#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>

#include <gtest/gtest.h>

extern "C" {
#include <libavformat/avformat.h>
}

#include "mvp/capture_devices.h"
#include "mvp/recorder.h"
#include "mvp/source_probe.h"
#include "test_support.h"

namespace mvp {
namespace {

using namespace std::chrono_literals;

// What a demuxer sees in one stream of a finished recording.
struct StreamSummary {
    int packets{0};
    double end_seconds{0.0};  // Largest pts + duration
};

StreamSummary Summarize(const std::string& path, AVMediaType type) {
    StreamSummary summary;
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        ADD_FAILURE() << "cannot open " << path;
        return summary;
    }
    avformat_find_stream_info(ctx, nullptr);
    const int index = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
    AVPacket* pkt = av_packet_alloc();
    while (index >= 0 && av_read_frame(ctx, pkt) >= 0) {
        if (pkt->stream_index == index) {
            ++summary.packets;
            if (pkt->pts != AV_NOPTS_VALUE) {
                const double end = (pkt->pts + pkt->duration) * av_q2d(ctx->streams[index]->time_base);
                summary.end_seconds = std::max(summary.end_seconds, end);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&ctx);
    return summary;
}

// 640x360 at the top-left of the primary monitor, 30 fps, H.264 + AAC.
RecordOptions RegionRecording(const std::string& path) {
    RecordOptions options;
    options.video.mode = CaptureMode::kRegion;
    options.video.region = {0, 0, 640, 360};
    options.video.frame_rate = 30;
    options.video_encoder.codec_name = "libx264";
    options.video_encoder.crf = 30;
    options.audio_encoder.codec_name = "aac";
    options.audio_encoder.rate_control = RateControlMode::kBitrate;
    options.audio_encoder.bitrate_bps = 128000;
    options.output_path = path;
    return options;
}

// Drives one recording: `script` runs between Start and Stop; then waits for
// the completion. kFailed if it did not complete in time.
RecordResult Record(Recorder& recorder, const RecordOptions& options, const std::function<void()>& script) {
    std::promise<RecordResult> done;
    std::future<RecordResult> result = done.get_future();
    recorder.SetCompletionCallback([&done](RecordResult r) { done.set_value(r); });
    if (!recorder.Start(options)) {
        ADD_FAILURE() << "the recording did not start";
        return RecordResult::kFailed;
    }
    script();
    recorder.Stop();
    const RecorderState after_stop = recorder.State();
    EXPECT_TRUE(after_stop == RecorderState::kStopping || after_stop == RecorderState::kIdle);
    if (result.wait_for(30s) != std::future_status::ready) {
        ADD_FAILURE() << "the recording did not complete";
        return RecordResult::kFailed;
    }
    EXPECT_EQ(recorder.State(), RecorderState::kIdle);
    return result.get();
}

RecordResult RecordFor(const RecordOptions& options, std::chrono::milliseconds duration) {
    Recorder recorder;
    return Record(recorder, options, [duration] { std::this_thread::sleep_for(duration); });
}

void ExpectTwoSecondsOfVideo(const std::string& path) {
    const SourceInfo info = SourceProbe::Probe(path);
    ASSERT_EQ(info.video_streams.size(), 1u);
    EXPECT_EQ(info.video_streams[0].width, 640);
    EXPECT_EQ(info.video_streams[0].height, 360);
    const StreamSummary video = Summarize(path, AVMEDIA_TYPE_VIDEO);
    EXPECT_GE(video.packets, 40) << "most 30 fps slots of 2 s should be captured";
    EXPECT_LE(video.packets, 62);
    EXPECT_NEAR(video.end_seconds, 2.0, 0.3);
}

TEST(CaptureDevicesTest, ListsMonitorsWithOnePrimaryAtTheOrigin) {
    const std::vector<MonitorInfo> monitors = CaptureDevices::Monitors();
    ASSERT_FALSE(monitors.empty());
    EXPECT_EQ(std::count_if(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; }), 1);
    for (const MonitorInfo& monitor : monitors) {
        EXPECT_FALSE(monitor.id.empty());
        EXPECT_FALSE(monitor.bounds.IsEmpty());
        if (monitor.primary) {
            EXPECT_EQ(monitor.bounds.x, 0);
            EXPECT_EQ(monitor.bounds.y, 0);
        }
    }
}

TEST(CaptureDevicesTest, DeviceListsAreWellFormed) {
    for (AudioDeviceKind kind : {AudioDeviceKind::kOutput, AudioDeviceKind::kInput}) {
        const std::vector<AudioDeviceInfo> devices = CaptureDevices::AudioDevices(kind);
        EXPECT_LE(std::count_if(devices.begin(), devices.end(), [](const auto& d) { return d.is_default; }), 1);
        for (const AudioDeviceInfo& device : devices) {
            EXPECT_FALSE(device.id.empty());
        }
    }
    for (const CameraInfo& camera : CaptureDevices::Cameras()) {
        EXPECT_FALSE(camera.id.empty());
    }
    for (const WindowInfo& window : CaptureDevices::Windows()) {
        EXPECT_NE(window.id, 0u);
        EXPECT_FALSE(window.bounds.IsEmpty());
    }
}

TEST(CaptureDevicesTest, Libx264IsAlwaysAnAvailableEncoder) {
    const std::vector<std::string> encoders = CaptureDevices::VideoEncoders();
    EXPECT_NE(std::find(encoders.begin(), encoders.end(), "libx264"), encoders.end());
}

TEST(RecorderTest, RecordsARegionWithGdi) {
    RecordOptions options = RegionRecording(test::ScratchPath("record_gdi.mp4"));
    options.video.method = ScreenCaptureMethod::kGdi;
    ASSERT_EQ(RecordFor(options, 2s), RecordResult::kSucceeded);
    ExpectTwoSecondsOfVideo(options.output_path);
    EXPECT_TRUE(SourceProbe::Probe(options.output_path).audio_streams.empty());
}

TEST(RecorderTest, RecordsARegionWithDesktopDuplication) {
    RecordOptions options = RegionRecording(test::ScratchPath("record_dxgi.mkv"));
    options.video.method = ScreenCaptureMethod::kDesktopDuplication;  // GDI if unavailable
    options.video.highlight_clicks = true;
    ASSERT_EQ(RecordFor(options, 2s), RecordResult::kSucceeded);
    ExpectTwoSecondsOfVideo(options.output_path);
}

TEST(RecorderTest, ReportsProgressWhileRecording) {
    Recorder recorder;
    RecordingStats stats;
    const RecordOptions options = RegionRecording(test::ScratchPath("record_stats.mp4"));
    ASSERT_EQ(Record(recorder, options, [&] {
                  std::this_thread::sleep_for(1500ms);
                  stats = recorder.Stats();
              }),
              RecordResult::kSucceeded);
    EXPECT_NEAR(stats.elapsed_seconds, 1.5, 0.3);
    EXPECT_GT(stats.frames_captured, 25);
    EXPECT_GT(stats.bytes_written, 0);
    const RecordingStats final_stats = recorder.Stats();
    EXPECT_GE(final_stats.elapsed_seconds, stats.elapsed_seconds);
    EXPECT_LT(final_stats.elapsed_seconds, 2.0) << "the elapsed time freezes at Stop()";
}

// The paused second is cut out: 2 s recorded over 3 s of wall time.
TEST(RecorderTest, PausedTimeIsLeftOut) {
    Recorder recorder;
    const RecordOptions options = RegionRecording(test::ScratchPath("record_pause.mp4"));
    ASSERT_EQ(Record(recorder, options, [&] {
                  std::this_thread::sleep_for(1s);
                  recorder.Pause();
                  EXPECT_EQ(recorder.State(), RecorderState::kPaused);
                  std::this_thread::sleep_for(1s);
                  recorder.Resume();
                  EXPECT_EQ(recorder.State(), RecorderState::kRecording);
                  std::this_thread::sleep_for(1s);
              }),
              RecordResult::kSucceeded);
    ExpectTwoSecondsOfVideo(options.output_path);
}

// Loopback delivers nothing while the system is silent: the mixer must turn
// that into silence covering the whole recording.
TEST(RecorderTest, RecordsSystemAudioEvenWhenSilent) {
    if (CaptureDevices::AudioDevices(AudioDeviceKind::kOutput).empty()) {
        GTEST_SKIP() << "no audio output device";
    }
    RecordOptions options = RegionRecording(test::ScratchPath("record_loopback.mp4"));
    options.system_audio.enabled = true;
    Recorder recorder;
    ASSERT_EQ(Record(recorder, options, [] { std::this_thread::sleep_for(2s); }), RecordResult::kSucceeded);
    ASSERT_TRUE(recorder.Sources().system_audio);

    const SourceInfo info = SourceProbe::Probe(options.output_path);
    ASSERT_EQ(info.audio_streams.size(), 1u);
    EXPECT_EQ(info.audio_streams[0].sample_rate, 48000);
    EXPECT_EQ(info.audio_streams[0].channels, 2);
    EXPECT_NEAR(Summarize(options.output_path, AVMEDIA_TYPE_AUDIO).end_seconds, 2.0, 0.3);
    ExpectTwoSecondsOfVideo(options.output_path);
}

TEST(RecorderTest, MixesTheMicrophoneWithSystemAudio) {
    if (CaptureDevices::AudioDevices(AudioDeviceKind::kInput).empty()) {
        GTEST_SKIP() << "no microphone";
    }
    RecordOptions options = RegionRecording(test::ScratchPath("record_mic.mp4"));
    options.system_audio.enabled = !CaptureDevices::AudioDevices(AudioDeviceKind::kOutput).empty();
    options.microphone.enabled = true;
    options.microphone.volume = 1.5f;
    Recorder recorder;
    ASSERT_EQ(Record(recorder, options, [] { std::this_thread::sleep_for(2s); }), RecordResult::kSucceeded);
    if (!recorder.Sources().microphone) {
        GTEST_SKIP() << "the microphone could not be opened (privacy settings?)";
    }
    ASSERT_EQ(SourceProbe::Probe(options.output_path).audio_streams.size(), 1u);
    EXPECT_NEAR(Summarize(options.output_path, AVMEDIA_TYPE_AUDIO).end_seconds, 2.0, 0.3);
}

TEST(RecorderTest, OverlaysTheCamera) {
    const std::vector<CameraInfo> cameras = CaptureDevices::Cameras();
    if (cameras.empty()) {
        GTEST_SKIP() << "no camera";
    }
    RecordOptions options = RegionRecording(test::ScratchPath("record_camera.mp4"));
    options.camera.enabled = true;
    options.camera.device_id = cameras[0].id;
    options.camera.corner = OverlayCorner::kTopLeft;
    Recorder recorder;
    ASSERT_EQ(Record(recorder, options, [] { std::this_thread::sleep_for(2s); }), RecordResult::kSucceeded);
    if (!recorder.Sources().camera) {
        GTEST_SKIP() << "the camera could not be opened (in use or blocked)";
    }
    ExpectTwoSecondsOfVideo(options.output_path);
}

// Optional sources that cannot be opened are left out; the recording goes on.
TEST(RecorderTest, RecordsWithoutOptionalSourcesThatFailToOpen) {
    RecordOptions options = RegionRecording(test::ScratchPath("record_fallback.mp4"));
    options.microphone.enabled = true;
    options.microphone.device_id = "{no-such-microphone}";
    options.camera.enabled = true;
    options.camera.device_id = "\\\\?\\no-such-camera";
    Recorder recorder;
    ASSERT_EQ(Record(recorder, options, [] { std::this_thread::sleep_for(2s); }), RecordResult::kSucceeded);
    EXPECT_FALSE(recorder.Sources().microphone);
    EXPECT_FALSE(recorder.Sources().camera);
    ExpectTwoSecondsOfVideo(options.output_path);
    EXPECT_TRUE(SourceProbe::Probe(options.output_path).audio_streams.empty());
}

TEST(RecorderTest, RefusesAnEmptyRegion) {
    RecordOptions options = RegionRecording(test::ScratchPath("record_empty.mp4"));
    options.video.region = {100, 100, 0, 0};
    Recorder recorder;
    EXPECT_FALSE(recorder.Start(options));
    EXPECT_EQ(recorder.State(), RecorderState::kIdle);
}

}  // namespace
}  // namespace mvp
