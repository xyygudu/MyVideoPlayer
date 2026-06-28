#ifndef MVP_GRAPH_NODE_H_
#define MVP_GRAPH_NODE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_format.h"

namespace mvp::graph {

class InputPort;
class OutputPort;
struct Command;

/// Categorizes node's role in the graph.
enum class NodeType {
    kSource,     // No inputs, produces data (e.g., DemuxNode)
    kTransform,  // Has inputs and outputs (e.g., DecoderNode, FilterNode)
    kSink,       // Has inputs, no outputs (e.g., VideoSinkNode)
};

/// Determines how a node is scheduled.
enum class ThreadingMode {
    kPassive,  // No own thread; Process() called synchronously by upstream
    kActive,   // Owns a worker thread; pulls from input Link
};

/// Lifecycle phase of a node.
enum class NodeState {
    kIdle,        // Initial state, not configured
    kConfigured,  // Configure() succeeded
    kPrepared,    // Prepare() succeeded, resources allocated
    kRunning,     // Start() called, thread active (Active) or ready (Passive)
    kPaused,      // Temporarily paused
    kError,       // An error occurred; requires Reset() to recover
};

/// Callback for Passive node output (supports 0/1/N outputs per input).
using OutputCallback = std::function<void(MediaBuffer)>;

/// Abstract interface for all graph processing nodes.
///
/// Lifecycle: [construct with config] → Negotiate → Prepare → Start → [Running] → Stop
/// Negotiate and Prepare must be called in order. State transitions are validated.
///
/// Configuration is node-specific (constructor params or setters), NOT part of
/// the polymorphic interface. Only lifecycle + data flow are polymorphic.
class INode {
  public:
    virtual ~INode() = default;

    // --- Lifecycle ---

    /// Declare/negotiate port formats with neighbors.
    /// Called by MediaGraph after all connections are made.
    virtual bool Negotiate() = 0;

    /// Allocate resources (codecs, surfaces, devices).
    /// Transitions: Idle/Configured → Prepared.
    virtual bool Prepare() = 0;

    /// Start processing (launch worker thread for Active nodes).
    /// Transitions: Prepared → Running.
    virtual bool Start() = 0;

    /// Stop processing and wait for thread exit.
    /// Transitions: Running/Paused → Idle.
    virtual void Stop() = 0;

    /// Clear internal state (buffers, decoder state) on seek.
    virtual void Flush() = 0;

    // --- Control ---

    /// Respond to a high-level control command (e.g. seek). Default no-op;
    /// nodes override to react (mechanism lives in the node, not the caller).
    virtual void OnCommand(const Command& cmd) { (void)cmd; }

    /// Pause or resume the node. Default no-op; sink nodes override to
    /// freeze/resume output. Backpressure naturally propagates upstream.
    virtual void SetPaused(bool paused) { (void)paused; }

    // --- Processing (Transform/Sink nodes) ---

    /// Process a single input buffer. Called synchronously for Passive nodes
    /// by the upstream Active node's thread.
    /// @param input  The buffer to process.
    /// @param emit   Callback to emit 0, 1, or N output buffers.
    virtual void Process(MediaBuffer input, OutputCallback emit) {
        (void)input;
        (void)emit;
    }

    // --- Port access ---

    virtual std::vector<InputPort*> Inputs() = 0;
    virtual std::vector<OutputPort*> Outputs() = 0;

    // --- Attributes ---

    virtual NodeType Type() const = 0;
    virtual ThreadingMode Threading() const = 0;
    virtual NodeState State() const = 0;

    /// Human-readable name for logging.
    virtual std::string Name() const = 0;
};

/// Stream descriptor produced by source probing.
struct StreamInfo {
    int index{-1};
    MediaType type{};
    MediaFormat format;    // EncodedFormat carrying a codec_params copy
    double duration{0.0};  // Source duration in seconds
};

/// A source node can probe its input to discover stream topology BEFORE the
/// full graph is built. This formalizes the inherent "inspect source first"
/// constraint (topology depends on source content) as an explicit phase,
/// distinct from the graph's unified Negotiate/Prepare lifecycle.
class ISourceNode : public INode {
  public:
    /// Open the source and return descriptors for all usable streams.
    /// Idempotent: repeated calls do not re-open the source.
    virtual std::vector<StreamInfo> Probe() = 0;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_NODE_H_
