#include "nodes/camera_capture_node.h"

#include <cmath>

extern "C" {
#include <libavutil/pixfmt.h>
}
#include <spdlog/spdlog.h>

#include "capture/camera_source.h"

namespace mvp::graph {
namespace {

constexpr double kReopenInterval = 1.0;

}  // namespace

CameraCaptureNode::CameraCaptureNode(std::string device_id)
    : source_(capture::CreateCameraSource(device_id)) {}

CameraCaptureNode::~CameraCaptureNode() { Stop(); }

bool CameraCaptureNode::OpenDevice(MediaFormat* format) {
    if (!source_->Open()) {
        SPDLOG_ERROR("CameraCaptureNode: cannot open the camera");
        return false;
    }
    const int fps = static_cast<int>(std::lround(source_->FrameRate()));
    *format = MediaFormat::Video(source_->Width(), source_->Height(), PixelFormat::kRGB32, Rational{fps, 1});
    return true;
}

void CameraCaptureNode::CloseDevice() { source_->Close(); }

void CameraCaptureNode::InterruptDevice() { source_->Interrupt(); }

void CameraCaptureNode::CaptureStep() {
    if (device_lost_) {
        if (!source_->Open()) {
            SleepFor(kReopenInterval);
            return;
        }
        SPDLOG_INFO("CameraCaptureNode: camera back");
        device_lost_ = false;
    }
    MediaFrame frame = pool_.Acquire(source_->Width(), source_->Height(), AV_PIX_FMT_BGRA);
    if (!frame.IsValid()) {
        SleepFor(kReopenInterval);
        return;
    }
    switch (source_->Read(frame.PlaneData(0), frame.PlaneLinesize(0))) {
        case capture::CameraSource::ReadStatus::kFrame:
            Emit(MediaBuffer(std::move(frame), Timestamp{Now()}));
            break;
        case capture::CameraSource::ReadStatus::kNoFrame:
            break;
        case capture::CameraSource::ReadStatus::kFailed:
            SPDLOG_WARN("CameraCaptureNode: camera lost, retrying every {} s", kReopenInterval);
            source_->Close();
            device_lost_ = true;
            SleepFor(kReopenInterval);
            break;
    }
}

}  // namespace mvp::graph
