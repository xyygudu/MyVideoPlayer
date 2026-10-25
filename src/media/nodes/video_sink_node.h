#ifndef MVP_NODES_VIDEO_SINK_NODE_H_
#define MVP_NODES_VIDEO_SINK_NODE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
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
///
/// Thread safety: all timing state and the renderer belong to the render
/// thread. The control thread only touches atomics (paused_,
/// preview_epoch_, pending_size_); a seek is observed by the render thread
/// through the epoch of the frames it pulls. The displayed frame is also
/// published under displayed_mutex_ for other threads (refresh, capture).
///
/// Bus messages: kFirstFrame once per epoch (the seek landed on screen),
/// kEos, and kDeviceLost when the renderer lost its GPU device.
class VideoSinkNode : public INode {
  public:
    VideoSinkNode();
    ~VideoSinkNode() override;

    // --- INode interface ---
    void DeclareCaps() override;
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

    /// Set the graph reference for epoch checks and EOS/error reporting.
    void Attach(MediaGraph* graph) override { graph_ = graph; }

    /// Set paused state (freezes render loop).
    void SetPaused(bool paused) override;

    /// Pts and epoch of the frame on screen; false before the first one.
    /// Any thread.
    bool DisplayedPosition(double* pts, int* serial) const;
    /// A new reference to the frame on screen (invalid before the first
    /// one) and its pts. Any thread.
    MediaFrame DisplayedFrame(double* pts) const;
    int64_t FramesPresented() const { return frames_presented_.load(std::memory_order_relaxed); }

  private:
    /// Render-thread timing state, restarted when a new seek epoch begins.
    struct Timeline {
        int serial{-1};                // Epoch the timing below belongs to
        bool fresh{true};              // Nothing presented in this epoch yet
        double last_pts{0.0};
        double last_display_time{0.0};
        std::optional<MediaBuffer> pending;  // Due frame held across a pause
    };

    enum class SlotWait { kReached, kPaused, kAbandoned };

    void RenderLoop();
    /// The frame held across a pause if still current, else the next pull.
    std::optional<MediaBuffer> NextBuffer(Timeline& timeline);
    void SyncAndRender(MediaBuffer buf, Timeline& timeline);
    /// Sleeps until the frame is due, in slices so a seek, pause, stop or
    /// resize is noticed within kMaxSleepSeconds.
    SlotWait WaitForSlot(double delay, int serial);
    /// Presents `buf` unless a seek made it stale meanwhile.
    void PresentNow(MediaBuffer buf, Timeline& timeline);
    void PresentFrame(MediaFrame frame, double pts, int serial);
    void RedrawCurrent();
    void RenderCurrent(int serial);
    void ApplyPendingResize();
    bool IsCurrentEpoch(int serial) const;
    void ReportEos(int serial);
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

    // Last displayed frame, kept so it can be re-presented without pulling
    // new data (window resize while paused). Only the render thread writes
    // it (under displayed_mutex_); it reads it without the lock.
    mutable std::mutex displayed_mutex_;
    MediaFrame current_frame_;
    double current_pts_{0.0};
    int current_serial_{0};
    std::atomic<int64_t> frames_presented_{0};

    // External references (non-owning)
    mvp::VideoRenderer* renderer_{nullptr};
    MediaGraph* graph_{nullptr};
    // Elected master; equal to clock_.get() when this node is the reference.
    IClock* master_clock_{nullptr};

    // Sync state (render thread only)
    double video_fps_{30.0};
    double frame_timer_{0.0};
    bool device_lost_reported_{false};

    // Thread
    std::thread render_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    // Seek epoch whose first frame is shown even while paused (ffplay steps
    // one frame after a paused seek). Tagged with the epoch so presenting a
    // frame only consumes the preview it belongs to; kNoPreview when none.
    static constexpr int kNoPreview = -1;
    std::atomic<int> preview_epoch_{kNoPreview};
    // Target window size packed as (width << 32) | height; 0 means none.
    // Packed so the render thread never reads a mismatched width/height pair.
    std::atomic<uint64_t> pending_size_{0};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_VIDEO_SINK_NODE_H_
