#ifndef MVP_PLAYER_H_
#define MVP_PLAYER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mvp/export.h"

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

    // State queries
    double Duration() const;
    double CurrentPosition() const;
    bool IsPlaying() const;

    // Callback registration
    using VideoFrameCallback = std::function<void(const uint8_t* data, int width, int height)>;
    void SetVideoFrameCallback(VideoFrameCallback cb);

  private:
    std::unique_ptr<PlayerImpl> impl_;
};

}  // namespace mvp

#endif  // MVP_PLAYER_H_
