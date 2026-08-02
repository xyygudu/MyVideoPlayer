#ifndef MVP_NODES_VIDEO_SINK_NODE_H_
#define MVP_NODES_VIDEO_SINK_NODE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "clock.h"
#include "graph/media_buffer.h"
#include "graph/media_graph.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp {
class VideoRenderer;
}  // namespace mvp

namespace mvp::graph {

/// Sink node: renders decoded video frames to a window via SDL3.
///
/// - NodeType: kSink (1 input, no output)
/// - ThreadingMode: kActive (owns render thread with sync timing)
///
/// This node contains the full A/V sync logic (frame_timer accumulation)
/// previously in PlayerImpl::VideoRenderLoop.
///
/// Lifecycle:
/// - VideoRenderer is created externally and passed in (non-owning).
///   This allows the app layer to manage the SDL window lifecycle.
/// - Owns a video time base and offers it for master-clock arbitration;
///   reads the elected master back from the graph during Negotiate.
class VideoSinkNode : public INode {
  public:
    VideoSinkNode();
    ~VideoSinkNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;
    void OnCommand(const Command& cmd) override;
    ClockOffer ProvideClock() override;

    void Process(MediaBuffer, OutputCallback) override {}

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override { return {}; }

    NodeType Type() const override { return NodeType::kSink; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "VideoSinkNode"; }

    // --- VideoSinkNode-specific ---

    /// Set the external VideoRenderer (must be valid for node lifetime).
    void SetRenderer(mvp::VideoRenderer* renderer);

    /// Set the graph reference for EOS reporting.
    void Attach(MediaGraph* graph) override { graph_ = graph; }

    /// Set paused state (freezes render loop).
    void SetPaused(bool paused) override;


  private:
    void RenderLoop();
    void SyncAndRender(MediaFrame& mf, double& last_pts, double& last_display_time);
    void RenderFrame(const MediaFrame& mf);
    double ComputeDisplayDelay(double pts, double last_pts,
                               double last_display_time);
    double ComputeSlavedDelay(double pts, double last_pts);
    double ComputeFreeRunDelay(double pts, double last_pts,
                               double last_display_time);

    NodeState state_{NodeState::kIdle};

    // Ports
    std::unique_ptr<InputPort> input_port_;

    // Video time base, written by the render thread on every displayed frame.
    std::shared_ptr<mvp::Clock> clock_{std::make_shared<mvp::Clock>()};

    // External references (non-owning)
    mvp::VideoRenderer* renderer_{nullptr};
    MediaGraph* graph_{nullptr};
    // Elected master; equal to clock_.get() when this node is the reference.
    IClock* master_clock_{nullptr};

    // Sync state
    double video_fps_{30.0};
    double frame_timer_{0.0};

    // Thread
    std::thread render_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> awating_preview_frame_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_VIDEO_SINK_NODE_H_
