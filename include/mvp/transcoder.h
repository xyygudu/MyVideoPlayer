#ifndef MVP_TRANSCODER_H_
#define MVP_TRANSCODER_H_

#include <functional>
#include <memory>
#include <string>

#include "mvp/export.h"
#include "mvp/transcode_options.h"

namespace mvp {

/// How a transcode run ended.
enum class TranscodeResult {
    kSucceeded,  // Output fully written, finalized and closed
    kCancelled,  // Cancel() was called; the partial output is finalized
    kFailed,     // Setup failed or a runtime error occurred (see the log)
};

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
/// Threading: call the setters, Start(), Cancel() and the destructor from
/// one owner thread; set the callbacks before Start().
///
/// Usage:
///   Transcoder t;
///   t.SetInput("in.mp4");
///   t.SetOutput("out.mp4", options);
///   t.SetProgressCallback([](double pct) { ... });
///   t.SetCompletionCallback([](TranscodeResult result) { ... });
///   t.Start();
class MVP_CORE_EXPORT Transcoder {
  public:
    Transcoder();
    /// Stops a running transcode without invoking the completion callback.
    ~Transcoder();

    Transcoder(const Transcoder&) = delete;
    Transcoder& operator=(const Transcoder&) = delete;

    void SetInput(const std::string& url);
    void SetOutput(const std::string& path, const TranscodeOptions& options);

    /// Builds the graph and starts full-speed processing. Returns false if
    /// a run is already active (nothing reported) or if the graph fails to
    /// build/negotiate/prepare/start (see logs); in the latter case the
    /// completion callback is invoked with kFailed before Start() returns.
    bool Start();

    /// Stops the active run early and reports kCancelled on the calling
    /// thread. No-op when no run is active.
    void Cancel();

    using ProgressCallback = std::function<void(double percent)>;
    using CompletionCallback = std::function<void(TranscodeResult result)>;

    /// Invoked with 0..100 after each muxed packet of the main stream, on the
    /// internal muxer thread: keep it short and never block in it.
    void SetProgressCallback(ProgressCallback cb);
    /// Invoked exactly once per started run. When the run ends by itself it
    /// runs on an internal event thread; it may then destroy the Transcoder
    /// or Start() a new run, but must not block waiting for the owner thread.
    void SetCompletionCallback(CompletionCallback cb);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_TRANSCODER_H_
