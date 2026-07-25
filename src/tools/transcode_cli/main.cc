#include <chrono>
#include <cstdio>
#include <thread>

#include "mvp/transcode_options.h"
#include "mvp/transcoder.h"

namespace {

/// Builds the v1 smoke-test options: software H.264 (CRF) video + AAC audio.
mvp::TranscodeOptions MakeDefaultOptions() {
    mvp::TranscodeOptions options;
    options.video.codec_name = "libx264";
    options.video.rate_control = mvp::RateControlMode::kCrf;
    options.video.crf = 23;
    options.video.preset = "medium";
    options.audio.codec_name = "aac";
    options.audio.bitrate_bps = 128000;
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input> <output.mp4>\n", argv[0]);
        std::fprintf(stderr,
                     "Manual validation tool for EncoderNode/MuxNode/Transcoder "
                     "(no GUI, no gtest — see openspec/changes/add-encoder-mux-nodes).\n");
        return 1;
    }

    mvp::Transcoder transcoder;
    transcoder.SetInput(argv[1]);
    transcoder.SetOutput(argv[2], MakeDefaultOptions());

    transcoder.SetProgressCallback([](double pct) {
        std::printf("\rProgress: %5.1f%%", pct);
        std::fflush(stdout);
    });

    bool done = false;
    bool success = false;
    transcoder.SetCompletionCallback([&done, &success](bool ok) {
        success = ok;
        done = true;
    });

    if (!transcoder.Start()) {
        std::fprintf(stderr, "\nTranscoder failed to start (see logs above).\n");
        return 1;
    }

    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\n%s\n",
                success ? "Transcode completed successfully." : "Transcode failed.");
    return success ? 0 : 1;
}
