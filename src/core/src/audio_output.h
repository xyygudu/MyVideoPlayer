#ifndef MVP_AUDIO_OUTPUT_H_
#define MVP_AUDIO_OUTPUT_H_

#include <atomic>
#include <thread>

struct SDL_AudioStream;
struct AVCodecContext;
struct AVStream;

namespace mvp {

class FrameQueue;
class PacketQueue;
class Decoder;
class Clock;

class AudioOutput {
  public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Initialize SDL audio with the given codec parameters.
    bool Open(AVStream* stream, PacketQueue* packet_queue, Clock* audio_clock);
    void Close();

    void Start();
    void Stop();
    void SetPaused(bool paused);
    void FlushFrameQueue();

    Decoder* GetDecoder() { return decoder_.get(); }

  private:
    void AudioLoop();

    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<FrameQueue> audio_frame_queue_;
    PacketQueue* packet_queue_;
    Clock* audio_clock_;
    AVStream* stream_;

    SDL_AudioStream* sdl_stream_;
    std::thread audio_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
};

}  // namespace mvp

#endif  // MVP_AUDIO_OUTPUT_H_
