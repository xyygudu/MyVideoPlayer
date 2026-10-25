#ifndef MVP_MEDIA_PLAYER_H_
#define MVP_MEDIA_PLAYER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mvp/captured_frame.h"
#include "mvp/effect_types.h"
#include "mvp/export.h"

namespace mvp {

/// Playback state (mirrors the old PlayerState).
enum class PlaybackState {
    kIdle,      // No file loaded
    kReady,     // File opened, graph prepared, ready to play
    kPlaying,   // Actively playing
    kPaused,    // Paused
    kFinished,  // Every stream played out; Play() restarts from the beginning
    kError,     // Open failed or playback hit an unrecoverable error (see the
                // log); output is frozen until Close() or the next Open()
};

/// Pipeline switches, mainly for debugging and compatibility.
struct PlayerOptions {
    bool hardware_decode{true};  // GPU (D3D11VA) decoding when supported; off = CPU
    bool gpu_effects{true};      // Effects on hardware frames run on the GPU; off = CPU
    bool zero_copy{true};        // Present hardware frames in place; off = download first
};

/// What the pipeline is actually doing, for diagnostics.
struct PipelineInfo {
    std::string video_codec;        // Empty without a video stream
    bool hardware_decoding{false};  // The video decoder runs on the GPU
    std::string renderer;           // SDL render backend, e.g. "direct3d11"
    int64_t frames_presented{0};    // Video frames shown since Open()
};

/// High-level media player built on the graph architecture.
///
/// This is the public API replacement for the old Player class.
/// Internally constructs a MediaGraph with:
///   DemuxNode → DecoderNode(video) → VideoSinkNode
///             → DecoderNode(audio) → AudioSinkNode
///
/// Thread safety: all methods are thread-safe. End-of-stream and runtime
/// errors are processed on an internal event thread, so State() can change
/// between calls without any call from the application.
///
/// Usage:
///   MediaPlayer player;
///   player.SetWindowHandle(hwnd);
///   player.Open("video.mp4");
///   player.Play();
///   ...
///   player.Close();
class MVP_CORE_EXPORT MediaPlayer {
  public:
    MediaPlayer();
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer&) = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    // --- Lifecycle ---

    bool Open(const std::string& filepath);
    void Close();

    // --- Playback control ---

    void Play();
    void Pause();
    void Seek(double position_seconds);

    // --- State queries ---

    PlaybackState State() const;
    double Duration() const;
    double CurrentPosition() const;
    double VideoFps() const;
    bool IsPlaying() const;

    // --- Window & rendering ---

    void SetWindowHandle(void* native_handle);
    void NotifyWindowResized(int width, int height);

    // --- Options & diagnostics ---

    /// Takes effect at once: an open source is rebuilt at the current
    /// position, keeping the play/pause state and the effect settings.
    void SetOptions(const PlayerOptions& options);
    PlayerOptions Options() const;
    PipelineInfo GetPipelineInfo() const;
    /// The frame on screen (after effects); width == 0 when there is none.
    CapturedFrame CaptureFrame() const;

    // --- Callbacks ---

    using FinishedCallback = std::function<void()>;

    /// Invoked once per playthrough when every stream has played out. Runs on
    /// the player's internal event thread: it may call Play/Pause/Seek and the
    /// state queries, but must not call Open()/Close() (ignored with an error
    /// log) nor block on a thread that might be inside Open()/Close(). GUI
    /// code should hop to its own thread first.
    void SetPlaybackFinishedCallback(FinishedCallback cb);

    // --- Effect chain ---
    // Changes made while paused (or finished) are shown right away: the frame
    // on screen is decoded again through the updated chain.

    /// Snapshot of every effect currently wired into the video pipeline
    /// (id, display name, enabled state, and parameters). Empty if no
    /// source is open.
    std::vector<EffectInfo> EffectInfos() const;

    /// Sets a single parameter of the named effect. Unknown effect_id or
    /// param_id is logged and ignored, never crashes.
    void SetEffectParam(const std::string& effect_id, const std::string& param_id,
                        EffectParamValue value);

    /// Enables or disables the named effect (bypasses it when disabled).
    /// Unknown effect_id is logged and ignored.
    void SetEffectEnabled(const std::string& effect_id, bool enabled);

    /// Restores every parameter of the named effect to its default (the
    /// enabled state is kept). Unknown effect_id is logged and ignored.
    void ResetEffectParams(const std::string& effect_id);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_MEDIA_PLAYER_H_
