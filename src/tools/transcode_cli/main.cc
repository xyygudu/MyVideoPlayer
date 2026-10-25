#include <cstdio>
#include <future>

#include "mvp/transcode_options.h"
#include "mvp/transcoder.h"

namespace {

/// Builds the v1 smoke-test options: software H.264 (CRF) video + AAC audio,
/// unless other encoders are named on the command line.
mvp::TranscodeOptions MakeOptions(int argc, char** argv) {
    mvp::TranscodeOptions options;
    options.video.codec_name = argc > 3 ? argv[3] : "libx264";
    options.video.rate_control = mvp::RateControlMode::kCrf;
    options.video.crf = 23;
    options.video.preset = "medium";
    options.audio.codec_name = argc > 4 ? argv[4] : "aac";
    options.audio.bitrate_bps = 128000;
    return options;
}

const char* Describe(mvp::TranscodeResult result) {
    switch (result) {
        case mvp::TranscodeResult::kSucceeded:
            return "Transcode completed successfully.";
        case mvp::TranscodeResult::kCancelled:
            return "Transcode cancelled.";
        default:
            return "Transcode failed.";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input> <output> [video_encoder] [audio_encoder]\n",
                     argv[0]);
        std::fprintf(stderr,
                     "Manual validation tool for EncoderNode/MuxNode/Transcoder "
                     "(no GUI, no gtest — see openspec/changes/add-encoder-mux-nodes).\n");
        return 1;
    }

    // Declared before the transcoder so it outlives the transcoder's threads.
    // The callback runs exactly once per started run (also when Start() fails).
    std::promise<mvp::TranscodeResult> finished;
    std::future<mvp::TranscodeResult> result = finished.get_future();

    mvp::Transcoder transcoder;
    transcoder.SetInput(argv[1]);
    transcoder.SetOutput(argv[2], MakeOptions(argc, argv));

    transcoder.SetProgressCallback([](double pct) {
        std::printf("\rProgress: %5.1f%%", pct);
        std::fflush(stdout);
    });
    transcoder.SetCompletionCallback(
        [&finished](mvp::TranscodeResult r) { finished.set_value(r); });

    if (!transcoder.Start()) {
        std::fprintf(stderr, "\nTranscoder failed to start (see logs above).\n");
        return 1;
    }

    const mvp::TranscodeResult outcome = result.get();
    std::printf("\n%s\n", Describe(outcome));
    return outcome == mvp::TranscodeResult::kSucceeded ? 0 : 1;
}
