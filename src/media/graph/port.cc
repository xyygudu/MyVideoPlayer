#include "graph/port.h"

#include <spdlog/spdlog.h>

namespace mvp::graph {

// --- InputPort ---

std::optional<MediaBuffer> InputPort::Pull() {
    if (!link_) {
        return std::nullopt;
    }
    while (auto buf = link_->Pop()) {
        const int epoch = CurrentEpoch();
        if (buf->serial() != epoch) {
            SPDLOG_DEBUG("InputPort[{}]: drop stale buffer serial={} epoch={}",
                         owner_->Name(), buf->serial(), epoch);
            continue;
        }
        if (!buf->IsValid()) {
            SPDLOG_WARN("InputPort[{}]: drop malformed buffer",
                        owner_->Name());
            continue;
        }
        return buf;
    }
    return std::nullopt;
}

// --- OutputPort ---

bool OutputPort::Connect(InputPort* peer, LinkCapacity capacity) {
    if (!peer) {
        SPDLOG_ERROR("OutputPort::Connect: peer is null");
        return false;
    }

    peer_ = peer;
    peer->peer_ = this;

    // Create a Link only if the downstream node is Active.
    // Passive downstream nodes are called synchronously via Process().
    if (peer->Owner()->Threading() == ThreadingMode::kActive) {
        link_ = std::make_unique<Link>(capacity);
        peer->link_ = link_.get();
    }

    return true;
}

void OutputPort::Push(MediaBuffer buf) {
    if (!peer_) {
        return;
    }

    INode* downstream = peer_->Owner();

    if (downstream->Threading() == ThreadingMode::kPassive) {
        // Synchronous call: Process on current thread.
        // The emit callback stamps each output with the input's seek epoch
        // and routes it to the Passive node's own output ports.
        const int serial = buf.serial();
        downstream->Process(std::move(buf), [downstream, serial](MediaBuffer out) {
            out.set_serial(serial);
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
