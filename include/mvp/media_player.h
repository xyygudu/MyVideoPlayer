#ifndef MVP_MEDIA_PLAYER_H_
#define MVP_MEDIA_PLAYER_H_

#include <functional>
#include <memory>
#include <string>

#include "mvp/export.h"
#include "mvp/video_frame.h"

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

    using VideoFrameCallback = std::function<void(const VideoFrame& frame)>;
    using FinishedCallback = std::function<void()>;

    void SetVideoFrameCallback(VideoFrameCallback cb);
    void SetPlaybackFinishedCallback(FinishedCallback cb);

    // --- Filter chain (Stop→Rebuild→Start) ---

    /// Apply a filter description (e.g., "scale=1280:720,eq=brightness=0.1").
    /// Stops the current graph, rebuilds with filter, seeks to saved position.
    /// Pass empty string to remove filter.
    void SetFilter(const std::string& filter_desc);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_MEDIA_PLAYER_H_
