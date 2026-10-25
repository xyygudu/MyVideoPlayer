#ifndef MVP_GRAPH_MESSAGE_BUS_H_
#define MVP_GRAPH_MESSAGE_BUS_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace mvp::graph {

class INode;

/// What a node reports to the application layer.
enum class GraphMessageType {
    kEos,         // A sink played out (or the muxer finalized) end-of-stream
    kError,       // A node hit a runtime error it cannot recover from
    kDeviceLost,  // The GPU device went away; the pipeline must be rebuilt
    kFirstFrame,  // A video sink presented the first frame of a seek epoch
};

struct GraphMessage {
    GraphMessageType type{GraphMessageType::kEos};
    const INode* source{nullptr};
    /// Seek epoch of the data that triggered the message. A later seek makes
    /// the message stale; the consumer compares it against the current epoch.
    int serial{0};
    std::string detail;
    double pts{0.0};  // kFirstFrame: presentation time of that frame
};

/// Carries GraphMessages from node worker threads to the single thread that
/// owns the application-facing state (GStreamer's GstBus, mpv's event queue).
/// Posting never blocks on the consumer, so a node can report while the
/// application is busy stopping the very graph the node belongs to.
///
/// Thread-safe: any thread may Post; one consumer thread calls Wait.
class MessageBus {
  public:
    /// Dropped silently once the bus is shut down.
    void Post(GraphMessage msg) {
        {
            std::lock_guard lock(mutex_);
            if (shutdown_) return;
            queue_.push_back(std::move(msg));
        }
        cond_.notify_one();
    }

    /// Blocks until a message arrives; nullopt once Shutdown() was called.
    std::optional<GraphMessage> Wait() {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
        return TakeLocked();
    }

    /// Like Wait(), but also returns nullopt after `timeout` without a message
    /// (tell the two apart with IsShutDown()), so a consumer can run deadlines.
    std::optional<GraphMessage> WaitFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        cond_.wait_for(lock, timeout, [this] { return shutdown_ || !queue_.empty(); });
        return TakeLocked();
    }

    bool IsShutDown() const {
        std::lock_guard lock(mutex_);
        return shutdown_;
    }

    /// Wakes the consumer for good and discards pending messages.
    void Shutdown() {
        {
            std::lock_guard lock(mutex_);
            shutdown_ = true;
            queue_.clear();
        }
        cond_.notify_all();
    }

  private:
    std::optional<GraphMessage> TakeLocked() {
        if (shutdown_ || queue_.empty()) return std::nullopt;
        GraphMessage msg = std::move(queue_.front());
        queue_.pop_front();
        return msg;
    }

    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<GraphMessage> queue_;  // Guarded by mutex_.
    bool shutdown_{false};            // Guarded by mutex_.
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MESSAGE_BUS_H_
