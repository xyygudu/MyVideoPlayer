#ifndef MVP_MEDIA_PLAYER_H_
#define MVP_MEDIA_PLAYER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mvp/effect_types.h"
#include "mvp/export.h"

namespace mvp {

/// Playback state (mirrors the old PlayerState).
enum class PlaybackState {
    kIdle,      // No file loaded
    kReady,     // File opened, graph prepared, ready to play
    kPlaying,   // Actively playing
    kPaused,    // Paused
    kFinished,  // Reached end-of-stream
    kError,     // An error occurred
};

/// High-level media player built on the graph architecture.
///
/// This is the public API replacement for the old Player class.
/// Internally constructs a MediaGraph with:
///   DemuxNode → DecoderNode(video) → VideoSinkNode
///             → DecoderNode(audio) → AudioSinkNode
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

    // --- Callbacks ---

    using FinishedCallback = std::function<void()>;

    void SetPlaybackFinishedCallback(FinishedCallback cb);

    // --- Effect chain ---

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

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_MEDIA_PLAYER_H_
