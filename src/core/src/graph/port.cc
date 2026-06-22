#include "graph/port.h"

#include <spdlog/spdlog.h>

namespace mvp::graph {

// --- InputPort ---

std::optional<MediaBuffer> InputPort::Pull() {
    if (!link_) {
        return std::nullopt;
    }
    return link_->Pop();
}

// --- OutputPort ---

bool OutputPort::Connect(InputPort* peer, int link_capacity) {
    if (!peer) {
        SPDLOG_ERROR("OutputPort::Connect: peer is null");
        return false;
    }

    // Check format compatibility if both have caps set.
    if (!caps_.IsEmpty() && !peer->Caps().IsEmpty()) {
        auto intersection = FormatCaps::Intersect(caps_, peer->Caps());
        if (intersection.IsEmpty() && !caps_.IsEmpty()) {
            SPDLOG_ERROR(
                "OutputPort::Connect: format caps incompatible between "
                "'{}' and '{}'",
                owner_->Name(), peer->Owner()->Name());
            return false;
        }
    }

    peer_ = peer;
    peer->peer_ = this;

    // Create a Link only if the downstream node is Active.
    // Passive downstream nodes are called synchronously via Process().
    if (peer->Owner()->Threading() == ThreadingMode::kActive) {
        link_ = std::make_unique<FrameLink>(CountCapacity{link_capacity});
        peer->link_ = link_.get();
    }

    // Propagate output format to downstream input port so that
    // downstream node can read it during Negotiate().
    peer->SetFormat(format_);

    return true;
}

void OutputPort::Push(MediaBuffer buf) {
    if (!peer_) {
        return;
    }

    INode* downstream = peer_->Owner();

    if (downstream->Threading() == ThreadingMode::kPassive) {
        // Synchronous call: Process on current thread.
        // The emit callback routes each output buffer to the Passive node's
        // own output ports (continuing the chain).
        downstream->Process(std::move(buf), [downstream](MediaBuffer out) {
            auto outputs = downstream->Outputs();
            if (!outputs.empty()) {
                outputs[0]->Push(std::move(out));
            }
        });
    } else {
        // Asynchronous: enqueue into the Link.
        if (link_) {
            link_->Push(std::move(buf));
        }
    }
}

void OutputPort::FlushLink() {
    if (link_) {
        link_->Flush();
    }
}

void OutputPort::AbortLink() {
    if (link_) {
        link_->Abort();
    }
}

}  // namespace mvp::graph
