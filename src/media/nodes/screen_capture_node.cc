#include "nodes/screen_capture_node.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/pixfmt.h>
}
#include <spdlog/spdlog.h>

#include "capture/screen_source.h"

namespace mvp::graph {
namespace {

constexpr int kMinFrameRate = 1;
constexpr int kMaxFrameRate = 120;
// Polling period while paused (nothing is due: the running time stands still).
constexpr double kPausedPoll = 0.02;

}  // namespace

ScreenCaptureNode::ScreenCaptureNode(VideoCaptureOptions options)
    : options_(std::move(options)),
      interval_(1.0 / std::clamp(options_.frame_rate, kMinFrameRate, kMaxFrameRate)) {}

ScreenCaptureNode::~ScreenCaptureNode() { Stop(); }

bool ScreenCaptureNode::OpenDevice(MediaFormat* format) {
    source_ = capture::CreateScreenSource(options_);
    if (!source_->Open()) {
        SPDLOG_ERROR("ScreenCaptureNode: cannot capture the requested area");
        return false;
    }
    SPDLOG_INFO("ScreenCaptureNode: {} at {} fps", source_->Description(), std::lround(1.0 / interval_));
    const int fps = static_cast<int>(std::lround(1.0 / interval_));
    *format = MediaFormat::Video(source_->Width(), source_->Height(), PixelFormat::kRGB32, Rational{fps, 1});
    return true;
}

void ScreenCaptureNode::CloseDevice() {
    if (source_) {
        source_->Close();
        source_.reset();
    }
    const int64_t captured = FramesCaptured();
    const int64_t dropped = FramesDropped();
    if (captured + dropped > 0) {
        SPDLOG_INFO("ScreenCaptureNode: {} frame(s) captured, {} slot(s) dropped", captured, dropped);
    }
}

bool ScreenCaptureNode::SlotDue() {
    const double now = Now();
    const double due = static_cast<double>(next_slot_) * interval_;
    if (now < due) {
        SleepFor(due - now);
        return false;
    }
    const auto current = static_cast<int64_t>(std::floor(now / interval_));
    if (current > next_slot_) {
        dropped_.fetch_add(current - next_slot_, std::memory_order_relaxed);
        next_slot_ = current;
    }
    return true;
}

void ScreenCaptureNode::CaptureStep() {
    if (Paused()) {
        SleepFor(kPausedPoll);
        return;
    }
    if (!SlotDue()) {
        return;
    }
    const double pts = static_cast<double>(next_slot_++) * interval_;
    // BGRA in memory, which PixelFormat::kRGB32 names on little-endian.
    MediaFrame frame = pool_.Acquire(source_->Width(), source_->Height(), AV_PIX_FMT_BGRA);
    if (!frame.IsValid()) {
        SPDLOG_WARN("ScreenCaptureNode: out of frame memory, slot dropped");
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!source_->Grab(frame.PlaneData(0), frame.PlaneLinesize(0))) {
        PostError("the screen can no longer be captured");
        SleepFor(1.0);
        return;
    }
    if (Emit(MediaBuffer(std::move(frame), Timestamp{pts}))) {
        captured_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace mvp::graph
