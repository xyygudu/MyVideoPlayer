#ifndef MVP_TRANSCODER_H_
#define MVP_TRANSCODER_H_

#include <functional>
#include <memory>
#include <string>

#include "mvp/export.h"
#include "mvp/transcode_options.h"

namespace mvp {

/// Offline transcode facade built on the graph architecture.
///
/// Independent of MediaPlayer — owns its own MediaGraph instance and runs
/// it at full speed (no Clock/AV-sync). Internally constructs:
///   Demux ─┬─► Decoder(V) → Encoder(V) ─┐
///          └─► Decoder(A) → Encoder(A) ──┤
///                                         └─► Mux
///
/// v1 scope: single video + single audio stream (or either alone), no
/// AVFilter/effect chain, no trim, no stream passthrough, no two-pass
/// encoding, software encode only.
///
/// Usage:
///   Transcoder t;
///   t.SetInput("in.mp4");
///   t.SetOutput("out.mp4", options);
///   t.SetProgressCallback([](double pct) { ... });
///   t.SetCompletionCallback([](bool ok) { ... });
///   t.Start();
class MVP_CORE_EXPORT Transcoder {
  public:
    Transcoder();
    ~Transcoder();

    Transcoder(const Transcoder&) = delete;
    Transcoder& operator=(const Transcoder&) = delete;

    void SetInput(const std::string& url);
    void SetOutput(const std::string& path, const TranscodeOptions& options);

    /// Builds the graph and starts full-speed processing. Returns false if
    /// the graph fails to build/negotiate/prepare (see logs for details);
    /// the completion callback is also invoked with false in that case.
    bool Start();

    /// Stops the graph early and reports completion with ok=false.
    void Cancel();

    using ProgressCallback = std::function<void(double percent)>;
    using CompletionCallback = std::function<void(bool ok)>;

    /// Invoked periodically (main-stream packets muxed) with 0..100.
    void SetProgressCallback(ProgressCallback cb);
    /// Invoked exactly once when the transcode finishes or fails.
    void SetCompletionCallback(CompletionCallback cb);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_TRANSCODER_H_
