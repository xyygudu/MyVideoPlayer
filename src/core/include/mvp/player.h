#ifndef MVP_PLAYER_H_
#define MVP_PLAYER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mvp/export.h"
#include "mvp/player_state.h"
#include "mvp/video_frame.h"

namespace mvp {

class PlayerImpl;

class MVP_CORE_EXPORT Player {
  public:
    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Lifecycle
    bool Open(const std::string& filepath);
    void Close();

    // Playback control
    void Play();
    void Pause();
    void Seek(double position_seconds);
    void StepFrame();

    // State queries
    double Duration() const;
    double CurrentPosition() const;
    double CurrentVideoPosition() const;
    double VideoFps() const;
    bool IsPlaying() const;
    PlayerState State() const;

    // Window handle for video rendering (pass native window handle for SDL embed)
    void SetWindowHandle(void* native_handle);
    void NotifyWindowResized(int width, int height);

    // Callback registration
    using VideoFrameCallback = std::function<void(const VideoFrame& frame)>;
    void SetVideoFrameCallback(VideoFrameCallback cb);
    void SetPlaybackFinishedCallback(std::function<void()> cb);

  private:
    std::unique_ptr<PlayerImpl> impl_;
};

}  // namespace mvp

#endif  // MVP_PLAYER_H_
