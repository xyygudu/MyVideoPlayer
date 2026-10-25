// Playback across codecs and containers: generated clips on every decode
// path, plus an opt-in pass over a folder of real files.
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "mvp/media_player.h"
#include "test_support.h"

namespace mvp {
namespace {

using namespace std::chrono_literals;
using test::WaitUntil;

// Plays `path` briefly, then seeks to the middle: frames must reach the
// screen and the clock must move both times.
void ExpectPlaysAndSeeks(MediaPlayer& player, const std::string& path) {
    ASSERT_TRUE(player.Open(path));
    player.Play();
    ASSERT_TRUE(WaitUntil([&] { return player.GetPipelineInfo().frames_presented >= 10; }, 10s))
        << "frames presented: " << player.GetPipelineInfo().frames_presented;
    const double start = player.CurrentPosition();
    ASSERT_TRUE(WaitUntil([&] { return player.CurrentPosition() > start + 0.5; }, 5s));
    EXPECT_EQ(player.State(), PlaybackState::kPlaying);
    EXPECT_GT(player.CaptureFrame().width, 0);

    const double target = player.Duration() / 2.0;
    player.Seek(target);
    ASSERT_TRUE(WaitUntil([&] { return player.CaptureFrame().pts >= target - 0.1; }, 10s))
        << "no frame shown after seeking to " << target << "s";
    EXPECT_LT(player.CaptureFrame().pts, target + 2.0);
    EXPECT_NE(player.State(), PlaybackState::kError);
}

struct CodecCase {
    const char* clip;
    bool hardware_decode;
};

std::string CaseName(const ::testing::TestParamInfo<CodecCase>& info) {
    std::string name = info.param.clip;
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    }
    return name + (info.param.hardware_decode ? "_Hw" : "_Sw");
}

class CodecPlaybackTest : public ::testing::TestWithParam<CodecCase> {
  protected:
    test::HiddenWindow window_{640, 360};
    MediaPlayer player_;
};

TEST_P(CodecPlaybackTest, PlaysAndSeeks) {
    PlayerOptions options;
    options.hardware_decode = GetParam().hardware_decode;
    player_.SetOptions(options);
    player_.SetWindowHandle(window_.handle());
    player_.NotifyWindowResized(640, 360);
    ExpectPlaysAndSeeks(player_, test::MediaPath(GetParam().clip));
    player_.Close();
}

const CodecCase kCodecCases[] = {
    {"hevc_main10.mkv", true}, {"hevc_main10.mkv", false}, {"av1.mkv", true},
    {"av1.mkv", false},        {"vp9_opus.webm", true},    {"vp9_opus.webm", false},
    {"mpeg2_mp2.ts", true},    {"mpeg2_mp2.ts", false},    {"mjpeg_pcm.avi", false},
};

INSTANTIATE_TEST_SUITE_P(Clips, CodecPlaybackTest, ::testing::ValuesIn(kCodecCases), CaseName);

// Every video in MVP_USER_MEDIA_DIR, e.g. a folder of real-world samples.
TEST(UserMediaTest, PlaysEveryFileInTheFolder) {
    const std::string dir = test::GetEnv("MVP_USER_MEDIA_DIR");
    if (dir.empty()) {
        GTEST_SKIP() << "set MVP_USER_MEDIA_DIR to a folder of videos to run this";
    }
    const std::set<std::string> extensions = {".mp4", ".mov", ".mkv", ".avi", ".webm",
                                              ".ts",  ".m2ts", ".flv", ".wmv"};
    int played = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!entry.is_regular_file() || extensions.count(ext) == 0) {
            continue;
        }
        SCOPED_TRACE(entry.path().filename().u8string());
        test::HiddenWindow window(960, 540);
        MediaPlayer player;
        player.SetWindowHandle(window.handle());
        player.NotifyWindowResized(960, 540);
        ExpectPlaysAndSeeks(player, entry.path().u8string());
        player.Close();
        ++played;
    }
    EXPECT_GT(played, 0) << "no videos in " << dir;
}

}  // namespace
}  // namespace mvp
