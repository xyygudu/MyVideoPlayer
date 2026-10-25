#ifndef MVP_NODES_LIVE_SOURCE_NODE_H_
#define MVP_NODES_LIVE_SOURCE_NODE_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"
#include "sync/clock.h"

namespace mvp::graph {

class MediaGraph;

/// Base of the capture sources (screen, camera, audio devices). It solves
/// what every live source shares, the way GStreamer's GstBaseSrc does for
/// live elements:
///
/// - Device thread: capture APIs are thread-affine (COM apartments, DXGI
///   duplication, per-thread DPI awareness), so one thread opens the device
///   during Open() (the real format is then known for negotiation), waits
///   for Start(), captures, and closes the device before exiting.
/// - Running time: every source offers a clock frozen at 0 until it starts.
///   All sources stamp with the elected master clock, so they share one
///   timeline; the graph pauses every clock, so paused time is excluded,
///   and sources drop what they capture meanwhile.
/// - End of stream on request: CommandType::kEndOfStream makes the device
///   thread stop capturing and push EOS (GStreamer's send-EOS), so encoders
///   drain and the muxer finalizes the file. Stop() aborts without EOS.
///
/// Derived classes implement the device hooks and must call Stop() in their
/// destructor (the device thread calls their hooks).
class LiveSourceNode : public INode {
  public:
    ~LiveSourceNode() override;

    bool Open() final;
    ClockOffer ProvideClock() override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override {}
    void OnCommand(const Command& cmd) override;
    void SetPaused(bool paused) override;

    std::vector<InputPort*> Inputs() override { return {}; }
    std::vector<OutputPort*> Outputs() override { return {output_port_.get()}; }
    NodeType Type() const override { return NodeType::kSource; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    void Attach(MediaGraph* graph) override { graph_ = graph; }

  protected:
    LiveSourceNode();

    // --- Device hooks, called on the device thread ---

    /// Acquires the device and describes what it will produce.
    virtual bool OpenDevice(MediaFormat* format) = 0;
    /// One step of capture: wait for/produce data and Emit() it. Called
    /// repeatedly while Capturing().
    virtual void CaptureStep() = 0;
    virtual void CloseDevice() = 0;

    /// Any thread: makes a CaptureStep() blocked in the device return soon.
    virtual void InterruptDevice() {}

    // --- Helpers for the hooks ---

    /// Running time in seconds on the graph's master clock.
    double Now() const;
    bool Capturing() const;
    bool Paused() const { return paused_.load(std::memory_order_acquire); }
    /// Sends a buffer downstream; dropped while paused. False once the
    /// downstream link is gone (the graph is stopping).
    bool Emit(MediaBuffer buf);
    /// Waits up to `seconds`; returns early when capture should end.
    void SleepFor(double seconds);
    void PostError(const std::string& detail);

  private:
    enum class OpenResult { kPending, kOpened, kFailed };

    void DeviceThread();
    /// Blocks until Start() or Stop(); true when capture should begin.
    bool WaitForStart();
    void EndStream();
    /// The clock every live source stamps with.
    IClock* TimeBase() const;

    std::unique_ptr<OutputPort> output_port_;
    std::shared_ptr<Clock> clock_;
    MediaGraph* graph_{nullptr};
    NodeState state_{NodeState::kIdle};  // Control thread only
    MediaFormat format_;                 // Written before Open() returns

    std::thread device_thread_;
    // Guards the fields below; cv_ wakes the device thread and Open().
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    OpenResult open_result_{OpenResult::kPending};
    bool start_requested_{false};
    bool eos_requested_{false};
    bool stop_requested_{false};

    std::atomic<bool> paused_{false};
    std::atomic<bool> downstream_gone_{false};  // Device thread only writes
};

}  // namespace mvp::graph

#endif  // MVP_NODES_LIVE_SOURCE_NODE_H_
