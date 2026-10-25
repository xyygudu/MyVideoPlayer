#ifndef MVP_NODES_AUDIO_SINK_NODE_H_
#define MVP_NODES_AUDIO_SINK_NODE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_graph.h"
#include "graph/node.h"
#include "graph/port.h"
#include "sync/clock.h"

struct SDL_AudioStream;

namespace mvp {
class AudioResampler;
}  // namespace mvp

namespace mvp::graph {

/// Sink node: plays decoded audio frames through SDL3 audio device.
///
/// - NodeType: kSink (1 input, no output)
/// - ThreadingMode: kActive (owns audio consumption thread)
///
/// This node replaces AudioRenderer by directly pulling MediaBuffers
/// containing audio MediaFrames from its input Link, resampling to the
/// fixed SDL stream format (S16, negotiated rate/channels; input changes
/// mid-stream are absorbed by the resampler), and feeding the SDL stream.
///
/// Lifecycle:
/// - AVStream* is needed for Prepare() to configure SDL audio device.
/// - Owns the audio time base and offers it for master-clock arbitration.
/// - Posts kEos to the graph bus once the device played everything out.
///
/// Thread safety:
/// - Worker thread owns the resampler and feeds SDL; the control thread
///   clears SDL on seek. Both hold feed_mutex_, and the worker re-checks the
///   seek epoch under it, so pre-seek audio can never be queued after the
///   clear nor move the clock.
/// - paused_ is atomically toggled from the control thread.
class AudioSinkNode : public INode {
  public:
    AudioSinkNode();
    ~AudioSinkNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;
    void OnCommand(const Command& cmd) override;
    void SetPaused(bool paused) override;
    ClockOffer ProvideClock() override;

    void Process(MediaBuffer, OutputCallback) override {}

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override { return {}; }

    NodeType Type() const override { return NodeType::kSink; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "AudioSinkNode"; }

    // --- AudioSinkNode-specific ---

    /// Set graph reference for epoch checks and EOS reporting.
    void Attach(MediaGraph* graph) override { graph_ = graph; }

  private:
    void AudioLoop();
    void CloseDevice();

    /// Clear the SDL queue (seek). Takes feed_mutex_.
    void FlushSdlBuffer();

    // Prepare helpers
    bool ReadAudioParams();
    bool OpenSdlDevice();

    // AudioLoop helpers
    bool ShouldThrottle() const;
    double QueuedSeconds() const;
    bool IsCurrentEpoch(int serial) const;
    void FeedFrame(const MediaBuffer& buf);
    void DrainAndReportEos(int serial);

    NodeState state_{NodeState::kIdle};

    // Ports
    std::unique_ptr<InputPort> input_port_;

    // Audio time base, written by the audio thread on every consumed frame.
    std::shared_ptr<mvp::Clock> clock_{std::make_shared<mvp::Clock>()};

    // External references (non-owning)
    MediaGraph* graph_{nullptr};

    // SDL audio state (owned, created in Prepare)
    ::SDL_AudioStream* sdl_stream_{nullptr};
    int sample_rate_{0};
    int channels_{0};

    // Created in Prepare, then used by the audio thread only.
    std::unique_ptr<AudioResampler> resampler_;
    AVFramePtr converted_;

    // Serializes feeding SDL (audio thread) with clearing it (control thread).
    std::mutex feed_mutex_;

    // Worker thread
    std::thread audio_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_AUDIO_SINK_NODE_H_
