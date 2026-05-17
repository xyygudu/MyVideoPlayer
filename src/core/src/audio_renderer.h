#ifndef MVP_AUDIO_RENDERER_H_
#define MVP_AUDIO_RENDERER_H_

#include <atomic>
#include <functional>
#include <thread>

struct SDL_AudioStream;
struct AVStream;

namespace mvp {

class MediaFrame;
template<typename T> class FrameQueue;
class PacketQueue;
class Clock;

/// AudioRenderer: consumes decoded audio frames from an external FrameQueue,
/// resamples to S16 if needed, and outputs via SDL3 audio.
///
/// Does NOT own Decoder or FrameQueue — those belong to StreamContext.
/// This keeps audio and video paths symmetric in ownership.
class AudioRenderer {
  public:
    using EofCallback = std::function<void()>;

    AudioRenderer();
    ~AudioRenderer();

    AudioRenderer(const AudioRenderer&) = delete;
    AudioRenderer& operator=(const AudioRenderer&) = delete;

    /// Initialize SDL audio device based on stream parameters.
    bool Open(AVStream* stream);
    void Close();

    /// Start the audio consumption thread.
    /// frame_queue: source of decoded audio frames (owned externally)
    /// packet_queue: used to check serial for stale frame discard
    /// audio_clock: updated with PTS of each consumed frame
    void Start(FrameQueue<MediaFrame>* frame_queue, PacketQueue* packet_queue,
               Clock* audio_clock);
    void Stop();

    void SetPaused(bool paused);
    void FlushSdlBuffer();

    /// Register a callback to be called when EOF marker is received.
    void SetEofCallback(EofCallback cb);

  private:
    void AudioLoop();

    FrameQueue<MediaFrame>* frame_queue_;
    PacketQueue* packet_queue_;
    Clock* audio_clock_;

    // Cached stream parameters (extracted at Open time)
    int sample_rate_{0};
    int channels_{0};

    SDL_AudioStream* sdl_stream_;
    std::thread audio_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    EofCallback eof_cb_;
};

}  // namespace mvp

#endif  // MVP_AUDIO_RENDERER_H_
