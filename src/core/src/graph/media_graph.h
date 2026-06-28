#ifndef MVP_GRAPH_MEDIA_GRAPH_H_
#define MVP_GRAPH_MEDIA_GRAPH_H_

#include <functional>
#include <memory>
#include <vector>

#include "graph/graph_command.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp {
class HWAccelContext;  // Forward declaration
}

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

/// Abstract clock interface for AV sync.
class IClock {
  public:
    virtual ~IClock() = default;
    virtual void Set(double pts) = 0;
    virtual double Get() const = 0;
    virtual void SetPaused(bool paused) = 0;
    virtual void SetSpeed(double speed) = 0;
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
    /// @param link_capacity  Capacity of the internal Link queue.
    ///                       Use large values (256) for packet links,
    ///                       small values (4-8) for frame links.
    bool Connect(OutputPort* src, InputPort* dst, int link_capacity = 4);

    // --- Lifecycle (cascades to all nodes in topo order) ---

    /// Run format negotiation on all nodes (Source → Sink order).
    /// Detects cycles. Returns false if any node fails.
    bool Negotiate();

    /// Allocate resources on all nodes.
    bool Prepare();

    /// Start all Active nodes' worker threads.
    bool Start();

    /// Stop all nodes, wait for threads to exit.
    void Stop();

    /// Broadcast flush to all nodes and links (seek).
    void Flush();

    // --- Control (high-level operations, decoupled from topology) ---

    /// Seek to a position: flush all links, then broadcast a seek command
    /// so each node resets its own internal state and repositions.
    void Seek(double position);

    /// Pause or resume playback (cascades to all nodes).
    void SetPaused(bool paused);

    /// Dispatch a command to all nodes in topological order. Nodes that do
    /// not handle the command type ignore it (default no-op).
    void SendCommand(const Command& cmd);

    // --- State & Clock ---

    GraphState State() const { return state_; }

    void SetClock(std::shared_ptr<IClock> clock) { clock_ = std::move(clock); }
    std::shared_ptr<IClock> Clock() const { return clock_; }

    void SetHWDevice(std::shared_ptr<mvp::HWAccelContext> hw) {
        hw_device_ = std::move(hw);
    }
    std::shared_ptr<mvp::HWAccelContext> HWDevice() const { return hw_device_; }

    void SetEventCallback(EventCallback cb) { event_cb_ = std::move(cb); }

    /// Report an event (called by nodes, thread-safe).
    void ReportEvent(GraphEvent event);

    // --- Query ---

    const std::vector<INode*>& Nodes() const { return node_ptrs_; }

  private:
    /// Compute topological order. Returns false if cycle detected.
    bool TopologicalSort();

    std::vector<std::unique_ptr<INode>> nodes_;
    std::vector<INode*> node_ptrs_;       // Raw pointers for convenience
    std::vector<INode*> topo_order_;      // Sorted execution order
    GraphState state_{GraphState::kIdle};
    std::shared_ptr<IClock> clock_;
    std::shared_ptr<mvp::HWAccelContext> hw_device_;
    EventCallback event_cb_;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MEDIA_GRAPH_H_
