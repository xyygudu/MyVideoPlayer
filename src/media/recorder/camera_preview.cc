#include "mvp/camera_preview.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "graph/media_graph.h"
#include "nodes/camera_capture_node.h"
#include "nodes/frame_callback_sink_node.h"

namespace mvp {
namespace {

// The newest frame matters, not every frame: a short queue keeps it fresh.
constexpr int kPreviewDepth = 2;

}  // namespace

/// A two-node graph: CameraCaptureNode -> FrameCallbackSinkNode.
class CameraPreview::Impl {
  public:
    ~Impl() { Stop(); }

    bool Start(const std::string& device_id, int max_width, FrameCallback on_frame);
    void Stop();
    bool IsRunning() const { return graph_ != nullptr; }

  private:
    std::unique_ptr<graph::MediaGraph> graph_;
};

bool CameraPreview::Impl::Start(const std::string& device_id, int max_width, FrameCallback on_frame) {
    Stop();
    graph_ = std::make_unique<graph::MediaGraph>();
    graph::INode* camera = graph_->AddNode(std::make_unique<graph::CameraCaptureNode>(device_id));
    graph::INode* sink =
        graph_->AddNode(std::make_unique<graph::FrameCallbackSinkNode>(max_width, std::move(on_frame)));
    graph_->Connect(camera->Outputs()[0], sink->Inputs()[0], graph::LinkCapacity::ForFrames(kPreviewDepth));
    if (!graph_->Open() || !graph_->Negotiate() || !graph_->Prepare() || !graph_->Start()) {
        SPDLOG_WARN("CameraPreview: cannot preview camera '{}'", device_id);
        Stop();
        return false;
    }
    return true;
}

void CameraPreview::Impl::Stop() {
    if (graph_) {
        graph_->Stop();
        graph_.reset();
    }
}

// --- CameraPreview public interface (delegates to Impl) ---

CameraPreview::CameraPreview() : impl_(std::make_unique<Impl>()) {}
CameraPreview::~CameraPreview() = default;

bool CameraPreview::Start(const std::string& device_id, int max_width, FrameCallback on_frame) {
    return impl_->Start(device_id, max_width, std::move(on_frame));
}
void CameraPreview::Stop() { impl_->Stop(); }
bool CameraPreview::IsRunning() const { return impl_->IsRunning(); }

}  // namespace mvp
