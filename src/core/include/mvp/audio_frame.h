#ifndef MVP_AUDIO_FRAME_H_
#define MVP_AUDIO_FRAME_H_

#include <cstdint>
#include <memory>

#include "mvp/export.h"

namespace mvp {

enum class SampleFormat {
    kUnknown = 0,
    kS16,
    kS32,
    kFloat,
    kS16Planar,
    kFloatPlanar,
};

class MVP_CORE_EXPORT AudioFrame {
  public:
    AudioFrame();
    ~AudioFrame();

    AudioFrame(AudioFrame&& other) noexcept;
    AudioFrame& operator=(AudioFrame&& other) noexcept;

    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;

    // Accessors
    const uint8_t* data() const;
    int nb_samples() const;
    int channels() const;
    int sample_rate() const;
    SampleFormat format() const;
    double pts() const;

    // Check if this frame holds valid data
    bool IsValid() const;

  private:
    friend class FrameConverter;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_AUDIO_FRAME_H_
