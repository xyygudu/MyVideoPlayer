#ifndef MVP_GRAPH_MEDIA_GRAPH_H_
#define MVP_GRAPH_MEDIA_GRAPH_H_

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "clock.h"
#include "graph/graph_command.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp::graph {

/// Events reported by the graph to the application.
enum class GraphEvent {
    kEos,            // All sinks reached end-of-stream
    kError,          // A node reported an error
    kStateChanged,   // GraphState changed
    kFormatChanged,  // A port's format changed during processing
};

/// Global state of the graph.
enum class GraphState {
    kIdle,      // No nodes configured
    kReady,     // All nodes Prepared, ready to Start
    kPlaying,   // All Active nodes running
    kPaused,    // Clock frozen, nodes waiting
    kFinished,  // All sinks reported EOS
    kError,     // A node is in error state
};

/// The media processing graph: manages node topology and lifecycle.
///
/// Usage:
///   1. AddNode() for each processing unit
///   2. Connect() ports between nodes
///   3. Negotiate() → Prepare() → Start()
///   4. Stop() when done
///
/// All lifecycle operations cascade to nodes in topological order.
class MediaGraph {
  public:
    using EventCallback = std::function<void(GraphEvent)>;

    MediaGraph();
    ~MediaGraph();

    // --- Topology construction ---

    /// Add a node to the graph. Returns raw pointer for configuration.
    INode* AddNode(std::unique_ptr<INode> node);

    /// Connect an output port to an input port.
    /// @param capacity  Buffering limits, built via LinkCapacity::ForPackets()
    ///                  or ForFrames(). No default — see OutputPort::Connect.
    bool Connect(OutputPort* src, InputPort* dst, LinkCapacity capacity);

    // --- Lifecycle (cascades to all nodes in topo order) ---

    /// Acquire external devices/files on all nodes. Must precede Negotiate.
    /// Rolls back already-opened nodes on failure.
    bool Open();

    /// Two-pass negotiation: DeclareCaps (Sink→Source) → caps validation →
    /// master clock arbitration → Negotiate (Source→Sink). Detects cycles.
    /// Returns false if any step fails.
    bool Negotiate();

    /// Allocate resources on all nodes.
    bool Prepare();

    /// Start all Active nodes' worker threads.
    bool Start();

    /// Stop all nodes, wait for threads to exit.
    void Stop();

    /// Drop all in-flight data: bumps the seek epoch (invalidating buffers
    /// already in transit) and clears every link and node.
    void Flush();

    // --- Control (high-level operations, decoupled from topology) ---

    /// Seek to a position: flush all links, broadcast a seek command so each
    /// node resets its own internal state, then reposition every clock.
    void Seek(double position);

    /// Pause or resume playback (cascades to all nodes, then all clocks).
    void SetPaused(bool paused);

    /// Dispatch a command to all nodes in topological order. Nodes that do
    /// not handle the command type ignore it (default no-op).
    void SendCommand(const Command& cmd);

    // --- State & Clock ---

    GraphState State() const { return state_; }

    /// The arbitrated master time base, or nullptr when no node offers one
    /// (e.g. a transcode graph). Non-owning.
    IClock* MasterClock() const { return master_clock_.get(); }

    /// Monotonic seek generation. Buffers stamped with an older value are
    /// stale in-flight data and get dropped at the port boundary.
    int SeekEpoch() const { return seek_epoch_.load(std::memory_order_acquire); }

    void SetEventCallback(EventCallback cb) { event_cb_ = std::move(cb); }

    /// Report an event (called by nodes, thread-safe).
    void ReportEvent(GraphEvent event);

    // --- Query ---

    const std::vector<INode*>& Nodes() const { return node_ptrs_; }

  private:
    /// Compute topological order. Returns false if cycle detected.
    bool TopologicalSort();

    /// Check every connection's caps for contradictions.
    bool ValidateCaps() const;

    /// Collect every node's ClockOffer and elect the highest-priority one.
    void SelectMasterClock();

    std::vector<std::unique_ptr<INode>> nodes_;
    std::vector<INode*> node_ptrs_;       // Raw pointers for convenience
    std::vector<INode*> topo_order_;      // Sorted execution order
    GraphState state_{GraphState::kIdle};
    std::atomic<int> seek_epoch_{0};
    std::vector<std::shared_ptr<IClock>> clocks_;  // Every offered clock
    std::shared_ptr<IClock> master_clock_;
    EventCallback event_cb_;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MEDIA_GRAPH_H_
