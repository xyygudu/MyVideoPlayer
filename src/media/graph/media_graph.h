#ifndef MVP_GRAPH_MEDIA_GRAPH_H_
#define MVP_GRAPH_MEDIA_GRAPH_H_

#include <atomic>
#include <memory>
#include <vector>

#include "clock.h"
#include "graph/graph_command.h"
#include "graph/message_bus.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp {
namespace gpu {
class GpuDevice;
}  // namespace gpu
}  // namespace mvp

namespace mvp::graph {

/// Lifecycle state of the graph. End-of-stream is not a graph state: it is
/// reported per sink on the message bus, and the facade decides what
/// "finished" means for its pipeline.
enum class GraphState {
    kIdle,     // No nodes configured
    kReady,    // All nodes Prepared, ready to Start
    kPlaying,  // All Active nodes running
    kPaused,   // Clock frozen, nodes waiting
    kError,    // A lifecycle step failed
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
///
/// Threading: lifecycle and control methods (Open … Stop, Flush, Seek,
/// SetPaused, SendCommand) must not run concurrently; the owning facade
/// serializes them. SeekEpoch() and Bus() are safe from any thread.
class MediaGraph {
  public:
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

    /// Start all Active nodes' worker threads. Only valid from kReady.
    bool Start();

    /// Stop all nodes, wait for threads to exit.
    void Stop();

    /// Drop all in-flight data: bumps the seek epoch (invalidating buffers
    /// already in transit) and clears every link and node.
    void Flush();

    // --- Control (high-level operations, decoupled from topology) ---

    /// Seek to a position: flush all links, broadcast a seek command so each
    /// node resets its own internal state, then reposition every clock in
    /// the new epoch.
    void Seek(double position);

    /// Pause or resume playback (cascades to all nodes, then all clocks).
    void SetPaused(bool paused);

    /// Dispatch a command to all nodes, sinks first (reverse topological
    /// order, like GStreamer's upstream seek event): consumers are ready for
    /// the new position before the source starts producing it. Nodes that do
    /// not handle the command type ignore it (default no-op).
    void SendCommand(const Command& cmd);

    // --- State & Clock ---

    GraphState State() const { return state_; }

    /// Set the GPU device shared by all nodes (injected by the pipeline
    /// builder before Open; decode/encode/effects derive their own contexts
    /// from it). Co-owned with the renderer that provides it.
    void SetGpuDevice(std::shared_ptr<gpu::GpuDevice> device);

    /// Non-owning accessor; valid until the graph is destroyed.
    gpu::GpuDevice* GpuDevice() const { return gpu_device_.get(); }

    /// The arbitrated master time base, or nullptr when no node offers one
    /// (e.g. a transcode graph). Non-owning.
    IClock* MasterClock() const { return master_clock_.get(); }

    /// Monotonic seek generation. Buffers stamped with an older value are
    /// stale in-flight data and get dropped at the port boundary.
    int SeekEpoch() const { return seek_epoch_.load(std::memory_order_acquire); }

    /// Node → application messages (EOS, runtime errors). Nodes post from
    /// their worker threads; the owning facade drains it on its own thread.
    MessageBus& Bus() { return bus_; }

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
    std::shared_ptr<gpu::GpuDevice> gpu_device_;   // Shared GPU device
    MessageBus bus_;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MEDIA_GRAPH_H_
