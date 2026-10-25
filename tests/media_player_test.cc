// MediaPlayer end to end: a real pipeline rendering into a hidden window,
// observed through the public API (state, position, captured frames).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "mvp/media_player.h"
#include "test_support.h"

namespace mvp {
namespace {

using namespace std::chrono_literals;
using test::MeanLuma;
using test::WaitUntil;

constexpr char kMainClip[] = "h264_aac_25fps.mp4";  // 6 s, 640x360, 25 fps, B-frames

const EffectParam* FindParam(const std::vector<EffectInfo>& effects, const std::string& effect_id,
                             const std::string& param_id) {
    for (const EffectInfo& effect : effects) {
        for (const EffectParam& param : effect.params) {
            if (effect.effect_id == effect_id && param.id == param_id) {
                return &param;
            }
        }
    }
    return nullptr;
}

class MediaPlayerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_NE(window_.handle(), nullptr);
        player_.SetWindowHandle(window_.handle());
        player_.NotifyWindowResized(640, 360);
    }

    void TearDown() override { player_.Close(); }

    void OpenAndPlay(const std::string& clip) {
        ASSERT_TRUE(player_.Open(test::MediaPath(clip)));
        player_.Play();
        ASSERT_TRUE(WaitUntil([this] { return player_.GetPipelineInfo().frames_presented > 0; }, 5s))
            << "no frame presented";
    }

    bool WaitForPosition(double seconds, std::chrono::milliseconds timeout = 10s) {
        return WaitUntil([&] { return player_.CurrentPosition() >= seconds; }, timeout);
    }

    bool WaitForState(PlaybackState state, std::chrono::milliseconds timeout = 10s) {
        return WaitUntil([&] { return player_.State() == state; }, timeout);
    }

    // Pauses and returns the frame on screen once it no longer changes.
    CapturedFrame PauseAndCapture() {
        player_.Pause();
        test::PumpFor(300ms);
        return player_.CaptureFrame();
    }

    // Waits for the paused frame to be redrawn so that `accept` holds.
    bool WaitForRedraw(const std::function<bool(const CapturedFrame&)>& accept, CapturedFrame* shown) {
        return WaitUntil([&] {
            *shown = player_.CaptureFrame();
            return shown->width > 0 && accept(*shown);
        }, 5s);
    }

    test::HiddenWindow window_{640, 360};
    MediaPlayer player_;  // Declared after the window: destroyed first.
};

TEST_F(MediaPlayerTest, PlaysToTheEndThenReplaysFromTheStart) {
    std::atomic<int> finished{0};
    player_.SetPlaybackFinishedCallback([&finished] { ++finished; });
    OpenAndPlay(kMainClip);
    player_.Seek(player_.Duration() - 1.0);

    ASSERT_TRUE(WaitForState(PlaybackState::kFinished));
    EXPECT_EQ(finished.load(), 1);

    player_.Play();
    EXPECT_EQ(player_.State(), PlaybackState::kPlaying);
    EXPECT_TRUE(WaitUntil([&] {
        const double pos = player_.CurrentPosition();
        return pos > 0.3 && pos < 3.0;
    }, 5s));
}

// After a seek past the last frame the software decoder must not keep
// skipping frames: the replay shows every frame again.
TEST_F(MediaPlayerTest, SoftwareDecodeReplaysEveryFrameAfterSeekingPastTheEnd) {
    PlayerOptions options;
    options.hardware_decode = false;
    player_.SetOptions(options);
    OpenAndPlay(kMainClip);
    EXPECT_FALSE(player_.GetPipelineInfo().hardware_decoding);

    player_.Seek(player_.Duration() + 1.0);
    ASSERT_TRUE(WaitForState(PlaybackState::kFinished));
    player_.Play();
    ASSERT_TRUE(WaitForPosition(0.5));
    const int64_t start = player_.GetPipelineInfo().frames_presented;
    ASSERT_TRUE(WaitForPosition(2.5));
    const int64_t shown = player_.GetPipelineInfo().frames_presented - start;
    EXPECT_GE(shown, 40) << "2 s at 25 fps is 50 frames";
}

// --- Effects on every processing path ---

struct EffectPath {
    const char* name;
    PlayerOptions options;
};

// GPU passes on hardware frames; CPU effects on downloaded (NV12) hardware
// frames; CPU effects on software-decoded (yuv420p) frames.
const EffectPath kEffectPaths[] = {
    {"Gpu", {true, true, true}},
    {"CpuOnDownloadedFrames", {true, false, true}},
    {"SoftwareDecode", {false, true, true}},
};

class EffectPathTest : public MediaPlayerTest, public ::testing::WithParamInterface<EffectPath> {
  protected:
    // Plays the main clip with this path's options and pauses at about 1 s.
    CapturedFrame PausedFrame() {
        player_.SetOptions(GetParam().options);
        OpenAndPlay(kMainClip);
        EXPECT_TRUE(WaitForPosition(1.0));
        return PauseAndCapture();
    }
};

TEST_P(EffectPathTest, PausedColorChangeRedrawsTheFrameOnScreen) {
    const CapturedFrame before = PausedFrame();
    ASSERT_GT(before.width, 0);

    player_.SetEffectParam("color", "brightness", EffectParamValue(0.4f));
    CapturedFrame after;
    ASSERT_TRUE(WaitForRedraw([&](const CapturedFrame& f) { return MeanLuma(f) > MeanLuma(before) + 20; },
                              &after))
        << "luma stayed at " << MeanLuma(after) << " (was " << MeanLuma(before) << ")";
    EXPECT_NEAR(after.pts, before.pts, 1e-3) << "the same frame must be redrawn";
    EXPECT_EQ(player_.State(), PlaybackState::kPaused);
}

TEST_P(EffectPathTest, PausedFlipRedrawsAnExactMirror) {
    const CapturedFrame before = PausedFrame();
    ASSERT_GT(before.width, 0);

    player_.SetEffectParam("transform", "flip_h", EffectParamValue(true));
    const CapturedFrame mirror = test::Mirrored(before);
    CapturedFrame after;
    const bool redrawn = WaitForRedraw(
        [&](const CapturedFrame& f) { return test::MeanAbsDiff(f, mirror) < 2.0; }, &after);
    if (!redrawn) {
        test::SaveImage(mirror, test::ScratchPath(std::string("flip_expected_") + GetParam().name + ".ppm"));
        test::SaveImage(after, test::ScratchPath(std::string("flip_actual_") + GetParam().name + ".ppm"));
    }
    ASSERT_TRUE(redrawn) << "mean difference to the mirror image: " << test::MeanAbsDiff(after, mirror);
    EXPECT_NEAR(after.pts, before.pts, 1e-3);
}

INSTANTIATE_TEST_SUITE_P(Paths, EffectPathTest, ::testing::ValuesIn(kEffectPaths),
                         [](const ::testing::TestParamInfo<EffectPath>& info) {
                             return std::string(info.param.name);
                         });

// A general transform resamples; every path must agree on the result (pure
// rotations used to snap to quarter turns on the CPU path).
TEST_F(MediaPlayerTest, TransformLooksTheSameOnEveryPath) {
    struct Setting {
        const char* param;
        float value;
    };
    const std::vector<std::vector<Setting>> transforms = {
        {{"rotate_deg", 20.0f}},
        {{"rotate_deg", 30.0f}, {"scale_x", 0.8f}, {"translate_x", 0.1f}},
    };
    OpenAndPlay(kMainClip);
    ASSERT_TRUE(WaitForPosition(1.0));
    const CapturedFrame original = PauseAndCapture();
    ASSERT_GT(original.width, 0);

    for (size_t t = 0; t < transforms.size(); ++t) {
        SCOPED_TRACE("transform #" + std::to_string(t));
        player_.ResetEffectParams("transform");
        for (const Setting& setting : transforms[t]) {
            player_.SetEffectParam("transform", setting.param, EffectParamValue(setting.value));
        }
        std::vector<CapturedFrame> shown;
        for (const EffectPath& path : kEffectPaths) {
            player_.SetOptions(path.options);
            CapturedFrame frame;
            ASSERT_TRUE(WaitForRedraw(
                [&](const CapturedFrame& f) { return std::abs(f.pts - original.pts) < 1e-3; }, &frame))
                << path.name;
            shown.push_back(std::move(frame));
        }
        EXPECT_GT(test::MeanAbsDiff(shown[0], original), 10.0) << "the transform changed nothing";
        for (size_t i = 1; i < shown.size(); ++i) {
            EXPECT_LT(test::MeanAbsDiff(shown[i], shown[0]), 3.0) << kEffectPaths[i].name << " vs Gpu";
        }
    }
}

TEST_F(MediaPlayerTest, ResetEffectParamsRestoresTheDefaults) {
    OpenAndPlay(kMainClip);
    ASSERT_TRUE(WaitForPosition(1.0));
    const CapturedFrame original = PauseAndCapture();
    ASSERT_GT(original.width, 0);

    player_.SetEffectParam("color", "brightness", EffectParamValue(0.4f));
    player_.SetEffectParam("color", "contrast", EffectParamValue(2.0f));
    CapturedFrame shown;
    ASSERT_TRUE(WaitForRedraw([&](const CapturedFrame& f) { return MeanLuma(f) > MeanLuma(original) + 20; },
                              &shown));

    player_.ResetEffectParams("color");
    for (const EffectInfo& effect : player_.EffectInfos()) {
        if (effect.effect_id != "color") continue;
        EXPECT_TRUE(effect.enabled);
        for (const EffectParam& param : effect.params) {
            EXPECT_EQ(param.value, param.default_value) << param.id;
        }
    }
    ASSERT_TRUE(WaitForRedraw(
        [&](const CapturedFrame& f) { return std::abs(MeanLuma(f) - MeanLuma(original)) < 1.0; }, &shown))
        << "luma " << MeanLuma(shown) << ", originally " << MeanLuma(original);
}

// Switching to software decoding rebuilds the pipeline: position, pause state
// and effect settings survive, and the CPU path renders what the GPU path did.
TEST_F(MediaPlayerTest, SwitchingDecodersKeepsPositionStateAndEffects) {
    OpenAndPlay(kMainClip);
    SPDLOG_INFO("test: hardware decoding before the switch: {}", player_.GetPipelineInfo().hardware_decoding);
    player_.SetEffectParam("color", "brightness", EffectParamValue(0.2f));
    ASSERT_TRUE(WaitForPosition(1.5));
    const CapturedFrame before = PauseAndCapture();
    ASSERT_GT(before.width, 0);

    PlayerOptions options;
    options.hardware_decode = false;
    player_.SetOptions(options);
    EXPECT_FALSE(player_.GetPipelineInfo().hardware_decoding);
    EXPECT_EQ(player_.State(), PlaybackState::kPaused);
    const std::vector<EffectInfo> effects = player_.EffectInfos();
    const EffectParam* brightness = FindParam(effects, "color", "brightness");
    ASSERT_NE(brightness, nullptr);
    EXPECT_EQ(brightness->value, EffectParamValue(0.2f));

    CapturedFrame after;
    ASSERT_TRUE(WaitForRedraw([](const CapturedFrame&) { return true; }, &after));
    EXPECT_NEAR(after.pts, before.pts, 1e-3);
    EXPECT_NEAR(MeanLuma(after), MeanLuma(before), 6.0);

    player_.Play();
    EXPECT_TRUE(WaitForPosition(before.pts + 0.5, 5s));
}

// A 2 fps clip: frames last 0.5 s. The video must follow the audio clock
// instead of running ahead (waits used to be capped at 0.1 s).
TEST_F(MediaPlayerTest, LowFrameRateVideoFollowsTheAudioClock) {
    OpenAndPlay("h264_aac_2fps.mp4");
    double worst_lead = -1.0;
    double worst_lag = 0.0;
    const auto end = std::chrono::steady_clock::now() + 4s;
    while (std::chrono::steady_clock::now() < end && player_.State() == PlaybackState::kPlaying) {
        const double position = player_.CurrentPosition();
        const double shown = player_.CaptureFrame().pts;
        worst_lead = std::max(worst_lead, shown - position);
        worst_lag = std::max(worst_lag, position - shown);
        test::PumpFor(50ms);
    }
    EXPECT_LT(worst_lead, 0.2) << "video ran ahead of the audio";
    EXPECT_LT(worst_lag, 0.75) << "video fell behind the audio";
}

// MPEG-TS timestamps start wherever the muxer left them (here 10 s); the
// player timeline starts at 0 anyway.
TEST_F(MediaPlayerTest, TransportStreamTimelineStartsAtZero) {
    OpenAndPlay("ts_offset10.ts");
    EXPECT_LT(player_.CurrentPosition(), 2.0);
    EXPECT_LT(player_.CaptureFrame().pts, 2.0);
    EXPECT_NEAR(player_.Duration(), 4.0, 0.5);
}

// Captures convert with the stream's own matrix and range.
TEST_F(MediaPlayerTest, CaptureKeepsTheSourceColors) {
    for (const char* clip : {"color_bt709_tv.mp4", "color_bt709_pc.mp4", "color_bt601_mjpeg.avi"}) {
        SCOPED_TRACE(clip);
        OpenAndPlay(clip);
        const CapturedFrame frame = PauseAndCapture();
        ASSERT_GT(frame.width, 0);
        const size_t center = (static_cast<size_t>(frame.height / 2) * frame.width + frame.width / 2) * 4;
        EXPECT_NEAR(frame.rgba[center + 0], 0x33, 4);
        EXPECT_NEAR(frame.rgba[center + 1], 0x66, 4);
        EXPECT_NEAR(frame.rgba[center + 2], 0xCC, 4);
        player_.Close();
    }
}

}  // namespace
}  // namespace mvp
