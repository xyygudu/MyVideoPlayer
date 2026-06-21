#ifndef MVP_GRAPH_PORT_H_
#define MVP_GRAPH_PORT_H_

#include <memory>
#include <optional>

#include "graph/link.h"
#include "graph/media_buffer.h"
#include "graph/media_format.h"
#include "graph/node.h"

namespace mvp::graph {

class INode;
class InputPort;
class OutputPort;

/// Input port — data consumption endpoint of a node.
/// Connected to an upstream OutputPort via a Link.
class InputPort {
  public:
    explicit InputPort(INode* owner) : owner_(owner) {}

    /// Pull a buffer from the upstream Link. Blocks if empty.
    /// Returns nullopt if the link is aborted.
    std::optional<MediaBuffer> Pull();

    /// Set the format capabilities this port accepts.
    void SetCaps(FormatCaps caps) { caps_ = std::move(caps); }
    const FormatCaps& Caps() const { return caps_; }

    /// Get/set the negotiated format (set after successful Connect).
    void SetFormat(MediaFormat format) { format_ = std::move(format); }
    const MediaFormat& Format() const { return format_; }

    INode* Owner() const { return owner_; }
    OutputPort* Peer() const { return peer_; }
    bool IsConnected() const { return peer_ != nullptr; }

  private:
    friend class OutputPort;

    INode* owner_;
    OutputPort* peer_{nullptr};
    FormatCaps caps_;
    MediaFormat format_;
    // Link is owned by the OutputPort that created it.
    FrameLink* link_{nullptr};
};

/// Output port — data production endpoint of a node.
/// Handles routing: Passive downstream → sync Process(); Active → Link.
class OutputPort {
  public:
    explicit OutputPort(INode* owner) : owner_(owner) {}

    /// Connect this output to a downstream input port.
    /// Creates a Link between them if both nodes are Active.
    /// @param link_capacity  Max items in the Link (default 4 for frames,
    ///                       use 256+ for packet links to avoid backpressure).
    /// Returns false if format caps are incompatible.
    bool Connect(InputPort* peer, int link_capacity = 4);

    /// Push a buffer to the downstream node.
    /// - If downstream is Passive: calls Process() synchronously
    ///   and routes the emitted buffers further downstream.
    /// - If downstream is Active: enqueues into the Link.
    void Push(MediaBuffer buf);

    /// Set the format capabilities this port can produce.
    void SetCaps(FormatCaps caps) { caps_ = std::move(caps); }
    const FormatCaps& Caps() const { return caps_; }

    /// Get/set the negotiated format.
    void SetFormat(MediaFormat format) { format_ = std::move(format); }
    const MediaFormat& Format() const { return format_; }

    INode* Owner() const { return owner_; }
    InputPort* Peer() const { return peer_; }
    bool IsConnected() const { return peer_ != nullptr; }

    /// Access the underlying link (only exists for Active downstream).
    FrameLink* GetLink() const { return link_.get(); }

    /// Flush the link if it exists.
    void FlushLink();

    /// Abort the link if it exists.
    void AbortLink();

  private:
    INode* owner_;
    InputPort* peer_{nullptr};
    FormatCaps caps_;
    MediaFormat format_;
    std::unique_ptr<FrameLink> link_;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_PORT_H_
