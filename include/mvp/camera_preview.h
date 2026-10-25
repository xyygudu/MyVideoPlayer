#ifndef MVP_CAMERA_PREVIEW_H_
#define MVP_CAMERA_PREVIEW_H_

#include <functional>
#include <memory>
#include <string>

#include "mvp/captured_frame.h"
#include "mvp/export.h"

namespace mvp {

/// Live frames from a camera, e.g. to show the user what the recording's
/// camera overlay will capture. A camera usually serves one client at a
/// time: stop the preview before a recording uses the same device.
///
/// Threading: call the methods from one owner thread; frames are delivered
/// on an internal thread.
class MVP_CORE_EXPORT CameraPreview {
  public:
    using FrameCallback = std::function<void(const CapturedFrame& frame)>;

    CameraPreview();
    ~CameraPreview();

    CameraPreview(const CameraPreview&) = delete;
    CameraPreview& operator=(const CameraPreview&) = delete;

    /// Opens the camera and starts delivering frames scaled to at most
    /// `max_width` pixels wide. False when the camera cannot be opened.
    bool Start(const std::string& device_id, int max_width, FrameCallback on_frame);
    void Stop();
    bool IsRunning() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_CAMERA_PREVIEW_H_
