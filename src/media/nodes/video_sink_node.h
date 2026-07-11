#ifndef MVP_NODES_VIDEO_SINK_NODE_H_
#define MVP_NODES_VIDEO_SINK_NODE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_graph.h"
#include "graph/node.h"
#include "graph/port.h"
#include "mvp/video_frame.h"

namespace mvp {
class VideoRenderer;
class Clock;
}  // namespace mvp

namespace mvp::graph {

/// Callback to deliver rendered VideoFrame to app layer.
using VideoFrameCallback = std::function<void(const VideoFrame&)>;

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
/// - Clock reference is obtained from MediaGraph for AV sync.
/// - Worker thread pulls frames from input Link and applies timing.
class VideoSinkNode : public INode {
  public:
    /// Sync strategy.
    enum class SyncMode { kAudioMaster, kVideoMaster };

    VideoSinkNode();
    ~VideoSinkNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;
    void OnCommand(const Command& cmd) override;

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

    /// Set the audio clock for AudioMaster sync.
    void SetAudioClock(mvp::Clock* audio_clock);

    /// Set video clock (updated by this node when displaying frames).
    void SetVideoClock(mvp::Clock* video_clock);

    void SetSyncMode(SyncMode mode) { sync_mode_ = mode; }
    void SetVideoFps(double fps) { video_fps_ = fps; }

    /// Callback for delivering rendered frames to app layer.
    void SetFrameCallback(VideoFrameCallback cb) { frame_cb_ = std::move(cb); }

    /// Set the graph reference for EOS reporting.
    void SetGraph(MediaGraph* graph) { graph_ = graph; }

    /// Set paused state (freezes render loop).
    void SetPaused(bool paused) override;


  private:
    void RenderLoop();
    void SyncAndRender(MediaFrame& mf, double& last_pts, double& last_display_time);
    void RenderFrame(const MediaFrame& mf);
    double ComputeDisplayDelay(double pts, double last_pts,
                               double last_display_time);
    double ComputeAudioMasterDelay(double pts, double last_pts);
    double ComputeVideoMasterDelay(double pts, double last_pts,
                                   double last_display_time);

    NodeState state_{NodeState::kIdle};

    // Ports
    std::unique_ptr<InputPort> input_port_;

    // External references (non-owning)
    mvp::VideoRenderer* renderer_{nullptr};
    mvp::Clock* audio_clock_{nullptr};
    mvp::Clock* video_clock_{nullptr};
    MediaGraph* graph_{nullptr};

    // Sync state
    SyncMode sync_mode_{SyncMode::kAudioMaster};
    double video_fps_{30.0};
    double frame_timer_{0.0};

    // Thread
    std::thread render_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> awating_preview_frame_{false};
    // App callback
    VideoFrameCallback frame_cb_;
};

}  // namespace mvp::graph

#endif  // MVP_NODES_VIDEO_SINK_NODE_H_
