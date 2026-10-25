#ifndef MVP_CAPTURE_AUDIO_SOURCE_H_
#define MVP_CAPTURE_AUDIO_SOURCE_H_

#include <functional>
#include <memory>
#include <string>

#include "mvp/capture_devices.h"

struct AVFrame;

namespace mvp::capture {

/// Captures an audio endpoint: a microphone, or an output device through
/// loopback ("what you hear").
///
/// Threading: Open, Read and Close run on one thread (the capture thread).
class AudioSource {
  public:
    using PacketSink = std::function<void(AVFrame* frame, double age_seconds)>;

    virtual ~AudioSource() = default;

    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual int SampleRate() const = 0;
    virtual int Channels() const = 0;
    /// AVSampleFormat of the frames Read() delivers.
    virtual int SampleFormat() const = 0;
    virtual std::string Description() const = 0;
    /// Hands every packet the device has buffered to `sink`, with how long
    /// ago its first sample was captured. Returns false once the device is
    /// gone (unplugged, disabled): Close() it and try Open() again later.
    virtual bool Read(const PacketSink& sink) = 0;
};

std::unique_ptr<AudioSource> CreateAudioSource(AudioDeviceKind kind, const std::string& device_id);

}  // namespace mvp::capture

#endif  // MVP_CAPTURE_AUDIO_SOURCE_H_
