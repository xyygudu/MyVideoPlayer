#ifndef MVP_NODES_AUDIO_SINK_NODE_H_
#define MVP_NODES_AUDIO_SINK_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_graph.h"
#include "graph/node.h"
#include "graph/port.h"

struct SwrContext;
struct SDL_AudioStream;

namespace mvp {
class Clock;
}

namespace mvp::graph {

/// Sink node: plays decoded audio frames through SDL3 audio device.
///
/// - NodeType: kSink (1 input, no output)
/// - ThreadingMode: kActive (owns audio consumption thread)
///
/// This node replaces AudioRenderer by directly pulling MediaBuffers
/// containing audio MediaFrames from its input Link, resampling to
/// SDL format (S16), and feeding the SDL audio stream.
///
/// Lifecycle:
/// - AVStream* is needed for Prepare() to configure SDL audio device.
/// - Clock* is updated with each consumed frame's PTS (MasterClock source).
/// - Graph reference for EOS reporting.
///
/// Thread safety:
/// - Worker thread owns SDL audio device and SwrContext.
/// - paused_ is atomically toggled from main thread.
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

    void Process(MediaBuffer, OutputCallback) override {}

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override { return {}; }

    NodeType Type() const override { return NodeType::kSink; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "AudioSinkNode"; }

    // --- AudioSinkNode-specific ---

    /// Set the clock this node updates (audio master clock).
    void SetAudioClock(mvp::Clock* clock);

    /// Set graph reference for EOS reporting.
    void SetGraph(MediaGraph* graph) { graph_ = graph; }

    /// Pause/resume audio playback.
    void SetPaused(bool paused);

    /// Clear SDL audio buffer (used on seek).
    void FlushSdlBuffer();

  private:
    void AudioLoop();
    void CloseDevice();

    NodeState state_{NodeState::kIdle};

    // Ports
    std::unique_ptr<InputPort> input_port_;

    // External references (non-owning)
    mvp::Clock* audio_clock_{nullptr};
    MediaGraph* graph_{nullptr};

    // SDL audio state (owned, created in Prepare)
    ::SDL_AudioStream* sdl_stream_{nullptr};
    int sample_rate_{0};
    int channels_{0};

    // Worker thread
    std::thread audio_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_AUDIO_SINK_NODE_H_
