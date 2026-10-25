#ifndef MVP_NODES_VIDEO_COMPOSITOR_NODE_H_
#define MVP_NODES_VIDEO_COMPOSITOR_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/node.h"
#include "graph/port.h"
#include "media_frame.h"
#include "mvp/recorder.h"

struct SwsContext;

namespace mvp::graph {

/// Draws a picture-in-picture overlay (the camera) onto the main video (the
/// screen), both BGRA. Active, two inputs: the main input paces the output;
/// the overlay input is drained without blocking and its newest frame is
/// drawn, like OBS's asynchronous sources or ffmpeg's overlay filter with
/// eof_action=pass. An overlay that stopped arriving (camera unplugged) is
/// hidden instead of freezing on screen.
class VideoCompositorNode : public INode {
  public:
    explicit VideoCompositorNode(CameraOverlayOptions options);
    ~VideoCompositorNode() override;

    void DeclareCaps() override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override {}

    std::vector<InputPort*> Inputs() override { return {main_input_.get(), overlay_input_.get()}; }
    std::vector<OutputPort*> Outputs() override { return {output_port_.get()}; }
    NodeType Type() const override { return NodeType::kTransform; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "VideoCompositorNode"; }

  private:
    struct Placement {
        int x{0};
        int y{0};
        int width{0};
        int height{0};
    };

    void ComposeLoop();
    /// Takes every queued overlay frame and keeps the newest, scaled.
    void DrainOverlay();
    void ScaleOverlay(const MediaFrame& source);
    void DrawOverlay(MediaFrame& canvas, double pts);
    Placement PlaceOverlay(int source_width, int source_height) const;

    const CameraOverlayOptions options_;
    std::unique_ptr<InputPort> main_input_;
    std::unique_ptr<InputPort> overlay_input_;
    std::unique_ptr<OutputPort> output_port_;
    NodeState state_{NodeState::kIdle};

    std::thread thread_;
    std::atomic<bool> running_{false};

    // Compositor thread only (set before it starts).
    int canvas_width_{0};
    int canvas_height_{0};
    SwsContext* sws_{nullptr};
    MediaFramePool overlay_pool_;
    MediaFrame overlay_;  // Newest overlay, already scaled; invalid until the first
    Placement overlay_place_;
    double overlay_pts_{0.0};
    bool overlay_ended_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_VIDEO_COMPOSITOR_NODE_H_
