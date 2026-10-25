#ifndef MVP_AUDIO_AUDIO_MIX_TIMELINE_H_
#define MVP_AUDIO_AUDIO_MIX_TIMELINE_H_

#include <cstdint>
#include <vector>

namespace mvp {

/// Places several planar float audio streams on one sample timeline by
/// their timestamps and mixes them: the core of the recorder's mixer, as
/// OBS's audio output mixes its sources on the pipeline's timeline.
///
/// Positions are absolute sample indices (running time x sample rate).
/// Each input keeps what it wrote from ReadPosition() on. Write() puts the
/// samples where their timestamp says, except that a start within
/// `tolerance` samples of where the input's previous write ended continues
/// it seamlessly (timestamp jitter). Beyond that a gap reads as silence and
/// an overlap is dropped; samples before ReadPosition() are too late and
/// dropped too. MixNext() sums the inputs (missing samples are silence) with
/// per-input gains, clips to [-1, 1] and advances ReadPosition().
///
/// Not thread-safe: owned by the mixer's thread.
class AudioMixTimeline {
  public:
    AudioMixTimeline(int inputs, int channels, int64_t tolerance);

    /// `planes[c]` holds `count` samples of channel c starting at `start`.
    void Write(int input, const float* const* planes, int count, int64_t start);

    /// Where the samples written by `input` end; ReadPosition() when it has
    /// nothing buffered.
    int64_t BufferedEnd(int input) const;
    int64_t ReadPosition() const { return read_position_; }

    /// Mixes the next `count` samples into `out[c]`. `gains[i]` scales input
    /// i; `peaks[i]` receives the peak of its scaled samples in this chunk.
    void MixNext(float* const* out, int count, const float* gains, float* peaks);

  private:
    static constexpr int64_t kNoEnd = INT64_MIN;

    struct Track {
        std::vector<std::vector<float>> channels;  // Samples from ReadPosition() on
        int64_t end{kNoEnd};                       // Where the last write ended
    };

    const int channels_;
    const int64_t tolerance_;
    std::vector<Track> tracks_;
    int64_t read_position_{0};
};

}  // namespace mvp

#endif  // MVP_AUDIO_AUDIO_MIX_TIMELINE_H_
