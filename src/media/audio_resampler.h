#ifndef MVP_AUDIO_RESAMPLER_H_
#define MVP_AUDIO_RESAMPLER_H_

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

struct AVFrame;
struct SwrContext;

namespace mvp {

/// Converts decoded audio of any sample format / rate / channel layout into
/// one fixed output format, rebuilding the SwrContext whenever the input
/// parameters change mid-stream (ffplay's audio_decode_frame does the same).
/// Output frames are sized with swr_get_out_samples, so rate conversion never
/// truncates samples.
///
/// Not thread-safe: owned and used by a single worker thread.
class AudioResampler {
  public:
    AudioResampler(AVSampleFormat out_format, int out_rate, const AVChannelLayout& out_layout);
    ~AudioResampler();

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /// Converts `in` into `out` (unreferenced first). `out->nb_samples` may
    /// be 0 while the resampler buffers input. Passing a null `in` drains the
    /// samples still held for rate conversion (end of stream). Returns false
    /// on failure; each new input configuration is reported only once.
    bool Convert(const AVFrame* in, AVFrame* out);

  private:
    /// Makes swr_ match `in`; false if that configuration cannot be built.
    bool EnsureConfigured(const AVFrame* in);
    bool SameInput(const AVFrame* in) const;

    SwrContext* swr_{nullptr};
    AVSampleFormat out_format_;
    int out_rate_;
    AVChannelLayout out_layout_{};

    // Input configuration swr_ was (or failed to be) built for.
    bool has_input_{false};
    int in_format_{-1};
    int in_rate_{0};
    AVChannelLayout in_layout_{};
};

}  // namespace mvp

#endif  // MVP_AUDIO_RESAMPLER_H_
