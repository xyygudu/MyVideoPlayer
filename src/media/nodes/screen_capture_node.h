#ifndef MVP_NODES_SCREEN_CAPTURE_NODE_H_
#define MVP_NODES_SCREEN_CAPTURE_NODE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "media_frame.h"
#include "mvp/recorder.h"
#include "nodes/live_source_node.h"

namespace mvp::capture {
class ScreenSource;
}  // namespace mvp::capture

namespace mvp::graph {

/// Captures a desktop area (monitor, region or window) as BGRA frames at a
/// constant rate, with the mouse cursor drawn in, like ffmpeg's gdigrab
/// (draw_mouse) and OBS's display capture.
///
/// Frame n is due at n / frame_rate on the running time. A grab that comes
/// late skips the slots it missed instead of bursting to catch up, and
/// counts them as dropped; the timestamps keep the gaps (a VFR stream), so
/// the video stays in sync with the audio when the machine is overloaded.
class ScreenCaptureNode : public LiveSourceNode {
  public:
    explicit ScreenCaptureNode(VideoCaptureOptions options);
    ~ScreenCaptureNode() override;

    std::string Name() const override { return "ScreenCaptureNode"; }

    // Any thread.
    int64_t FramesCaptured() const { return captured_.load(std::memory_order_relaxed); }
    int64_t FramesDropped() const { return dropped_.load(std::memory_order_relaxed); }

  private:
    bool OpenDevice(MediaFormat* format) override;
    void CaptureStep() override;
    void CloseDevice() override;
    /// Advances next_slot_ past slots that are already over; false while
    /// the next one is not due yet.
    bool SlotDue();

    const VideoCaptureOptions options_;
    const double interval_;  // Seconds per frame

    // Device thread only.
    std::unique_ptr<capture::ScreenSource> source_;
    MediaFramePool pool_;
    int64_t next_slot_{0};

    std::atomic<int64_t> captured_{0};
    std::atomic<int64_t> dropped_{0};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_SCREEN_CAPTURE_NODE_H_
