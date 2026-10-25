#ifndef MVP_NODES_AUDIO_MIXER_NODE_H_
#define MVP_NODES_AUDIO_MIXER_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio_mix_timeline.h"
#include "audio_resampler.h"
#include "ffmpeg_utils.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp::graph {

class MediaGraph;

/// Mixes live audio inputs into one 48 kHz stereo float stream, like OBS's
/// audio mixer or ffmpeg's amix: each input is resampled, placed on a shared
/// timeline by timestamp (AudioMixTimeline) and mixed in 1024-sample chunks
/// stamped with their timeline position.
///
/// A chunk goes out as soon as every running input has covered it, or at
/// the latest kLatency after its time on the master clock: a loopback
/// device delivers nothing at all while the system is silent, and such gaps
/// must become silence instead of stalling the mix. While paused the clock
/// stands still, so nothing is invented. Once every input has ended, the
/// mix is flushed up to the running time it ended at, and EOS sent.
///
/// Active, N inputs; polls them every 5 ms without blocking.
class AudioMixerNode : public INode {
  public:
    explicit AudioMixerNode(int inputs);
    ~AudioMixerNode() override;

    void DeclareCaps() override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override {}

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override { return {output_port_.get()}; }
    NodeType Type() const override { return NodeType::kTransform; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "AudioMixerNode"; }
    void Attach(MediaGraph* graph) override { graph_ = graph; }

    // --- Any thread ---

    /// Linear gain of `input` (clamped to 0..4).
    void SetGain(int input, float gain);
    /// Peak level of `input` (after gain, 0..1) since the previous call.
    float TakeLevel(int input);

  private:
    struct InputState {
        std::unique_ptr<InputPort> port;
        std::unique_ptr<AudioResampler> resampler;
        bool ended{false};
        std::atomic<float> gain{1.0f};
        std::atomic<float> level{0.0f};
    };

    void MixLoop();
    /// Moves everything queued on the inputs onto the timeline.
    void PullInputs();
    void WriteFrame(size_t input, const MediaBuffer& buf);
    void FinishInput(size_t input);
    /// Emits the chunks that are due; false once downstream is gone.
    bool EmitDueChunks(bool flushing);
    bool ChunkDue(int64_t chunk_end, int64_t deadline) const;
    bool EmitChunk();
    /// Mixing may proceed up to this sample position without full coverage.
    int64_t Deadline(bool flushing) const;
    /// End of the final flush: all that was written, and at least `ended_at`.
    int64_t FlushEnd(int64_t ended_at) const;

    std::vector<std::unique_ptr<InputState>> inputs_;
    std::unique_ptr<OutputPort> output_port_;
    MediaGraph* graph_{nullptr};
    NodeState state_{NodeState::kIdle};

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Mixer thread only.
    AudioMixTimeline timeline_;
    AVFramePtr resampled_;
};

}  // namespace mvp::graph

#endif  // MVP_NODES_AUDIO_MIXER_NODE_H_
