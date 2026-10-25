#ifndef MVP_NODES_CAMERA_CAPTURE_NODE_H_
#define MVP_NODES_CAMERA_CAPTURE_NODE_H_

#include <memory>
#include <string>

#include "base/media_frame.h"
#include "nodes/live_source_node.h"

namespace mvp::capture {
class CameraSource;
}  // namespace mvp::capture

namespace mvp::graph {

/// Captures a camera as BGRA frames at the camera's own rate, stamped with
/// the running time at which they arrive. A camera that fails (unplugged,
/// taken by another application) is reopened every second; the size may
/// differ afterwards, so consumers go by each frame's own size.
class CameraCaptureNode : public LiveSourceNode {
  public:
    explicit CameraCaptureNode(std::string device_id);
    ~CameraCaptureNode() override;

    std::string Name() const override { return "CameraCaptureNode"; }

  private:
    bool OpenDevice(MediaFormat* format) override;
    void CaptureStep() override;
    void CloseDevice() override;
    void InterruptDevice() override;

    // Created with the node so InterruptDevice() never races its creation;
    // otherwise used by the device thread only.
    const std::unique_ptr<capture::CameraSource> source_;
    MediaFramePool pool_;  // Device thread only
    bool device_lost_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_CAMERA_CAPTURE_NODE_H_
