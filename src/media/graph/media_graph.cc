#include "graph/media_graph.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "gpu/gpu_device.h"

namespace mvp::graph {

MediaGraph::MediaGraph() = default;

MediaGraph::~MediaGraph() {
    Stop();
    // Explicit order: nodes (which may still reference presentation pool
    // textures in retained frames) must go before the GPU device frees the
    // pool. Default member order would free the device first.
    nodes_.clear();
    node_ptrs_.clear();
    gpu_device_.reset();
}

INode* MediaGraph::AddNode(std::unique_ptr<INode> node) {
    INode* ptr = node.get();
    nodes_.push_back(std::move(node));
    node_ptrs_.push_back(ptr);
    ptr->Attach(this);
    return ptr;
}

void MediaGraph::SetGpuDevice(std::unique_ptr<gpu::GpuDevice> device) {
    gpu_device_ = std::move(device);
}

bool MediaGraph::Connect(OutputPort* src, InputPort* dst,
                         LinkCapacity capacity) {
    if (!src || !dst) {
        SPDLOG_ERROR("MediaGraph::Connect: null port");
        return false;
    }
    if (!src->Connect(dst, capacity)) {
        return false;
    }
    dst->BindSeekEpoch(&seek_epoch_);
    return true;
}

bool MediaGraph::TopologicalSort() {
    // Kahn's algorithm for DAG topological sort.
    // Build adjacency from port connections.
    std::unordered_map<INode*, int> in_degree;
    std::unordered_map<INode*, std::vector<INode*>> adjacency;

    for (auto* node : node_ptrs_) {
        in_degree[node] = 0;
    }

    for (auto* node : node_ptrs_) {
        for (auto* out_port : node->Outputs()) {
            if (out_port->IsConnected()) {
                INode* downstream = out_port->Peer()->Owner();
                adjacency[node].push_back(downstream);
                in_degree[downstream]++;
            }
        }
    }

    std::queue<INode*> zero_in;
    for (auto& [node, degree] : in_degree) {
        if (degree == 0) {
            zero_in.push(node);
        }
    }

    topo_order_.clear();
    while (!zero_in.empty()) {
        INode* current = zero_in.front();
        zero_in.pop();
        topo_order_.push_back(current);

        for (auto* neighbor : adjacency[current]) {
            if (--in_degree[neighbor] == 0) {
                zero_in.push(neighbor);
            }
        }
    }

    if (topo_order_.size() != node_ptrs_.size()) {
        SPDLOG_ERROR(
            "MediaGraph::TopologicalSort: cycle detected! "
            "Sorted {} of {} nodes",
            topo_order_.size(), node_ptrs_.size());
        return false;
    }

    return true;
}

bool MediaGraph::Open() {
    if (!TopologicalSort()) {
        state_ = GraphState::kError;
        return false;
    }

    for (size_t i = 0; i < topo_order_.size(); ++i) {
        if (topo_order_[i]->Open()) {
            continue;
        }
        SPDLOG_ERROR("MediaGraph::Open: node '{}' failed",
                     topo_order_[i]->Name());
        for (size_t j = 0; j < i; ++j) {
            topo_order_[j]->Stop();
        }
        state_ = GraphState::kError;
        return false;
    }

    return true;
}

bool MediaGraph::ValidateCaps() const {
    for (auto* node : node_ptrs_) {
        for (auto* out_port : node->Outputs()) {
            if (!out_port->IsConnected()) {
                continue;
            }
            if (!FormatCaps::Compatible(out_port->Caps(),
                                        out_port->Peer()->Caps())) {
                SPDLOG_ERROR("MediaGraph: incompatible caps '{}' -> '{}'",
                             node->Name(),
                             out_port->Peer()->Owner()->Name());
                return false;
            }
        }
    }
    return true;
}

void MediaGraph::SelectMasterClock() {
    clocks_.clear();
    master_clock_.reset();

    int best_priority = 0;
    for (auto* node : topo_order_) {
        ClockOffer offer = node->ProvideClock();
        if (!offer.clock) {
            continue;
        }
        clocks_.push_back(offer.clock);
        if (!master_clock_ || offer.priority > best_priority) {
            master_clock_ = offer.clock;
            best_priority = offer.priority;
        }
    }
}

bool MediaGraph::Negotiate() {
    if (!TopologicalSort()) {
        state_ = GraphState::kError;
        return false;
    }

    for (auto it = topo_order_.rbegin(); it != topo_order_.rend(); ++it) {
        (*it)->DeclareCaps();
    }

    if (!ValidateCaps()) {
        state_ = GraphState::kError;
        return false;
    }

    SelectMasterClock();

    for (auto* node : topo_order_) {
        if (!node->Negotiate()) {
            SPDLOG_ERROR("MediaGraph::Negotiate: node '{}' failed",
                         node->Name());
            state_ = GraphState::kError;
            return false;
        }
    }

    return true;
}

bool MediaGraph::Prepare() {
    for (auto* node : topo_order_) {
        if (!node->Prepare()) {
            SPDLOG_ERROR("MediaGraph::Prepare: node '{}' failed",
                         node->Name());
            state_ = GraphState::kError;
            return false;
        }
    }

    state_ = GraphState::kReady;
    return true;
}

bool MediaGraph::Start() {
    if (state_ != GraphState::kReady && state_ != GraphState::kPaused) {
        SPDLOG_WARN("MediaGraph::Start: invalid state transition from {}",
                    static_cast<int>(state_));
        return false;
    }

    for (auto* node : topo_order_) {
        if (node->Threading() == ThreadingMode::kActive) {
            if (!node->Start()) {
                SPDLOG_ERROR("MediaGraph::Start: node '{}' failed to start",
                             node->Name());
                state_ = GraphState::kError;
                return false;
            }
        }
    }

    state_ = GraphState::kPlaying;
    if (event_cb_) {
        event_cb_(GraphEvent::kStateChanged);
    }
    return true;
}

void MediaGraph::Stop() {
    if (state_ == GraphState::kIdle) {
        return;
    }

    // Abort all links first to unblock waiting threads.
    for (auto* node : node_ptrs_) {
        for (auto* out_port : node->Outputs()) {
            out_port->AbortLink();
        }
    }

    // Stop nodes in reverse topological order (Sinks first).
    for (auto it = topo_order_.rbegin(); it != topo_order_.rend(); ++it) {
        (*it)->Stop();
    }

    state_ = GraphState::kIdle;
    if (event_cb_) {
        event_cb_(GraphEvent::kStateChanged);
    }
}

void MediaGraph::Flush() {
    // Bump before clearing: clearing wakes producers blocked in Push and they
    // immediately enqueue the buffer they still hold, which carries the old
    // epoch. Keeping the two together makes the invariant impossible to skip.
    seek_epoch_.fetch_add(1, std::memory_order_release);

    // Flush all links.
    for (auto* node : node_ptrs_) {
        for (auto* out_port : node->Outputs()) {
            out_port->FlushLink();
        }
    }

    // Flush all nodes in topological order.
    for (auto* node : topo_order_) {
        node->Flush();
    }
}

void MediaGraph::SendCommand(const Command& cmd) {
    // Dispatch in topological order; nodes filter by command type.
    const auto& order = topo_order_.empty() ? node_ptrs_ : topo_order_;
    for (auto* node : order) {
        node->OnCommand(cmd);
    }
}

void MediaGraph::Seek(double position) {
    Flush();
    SendCommand({CommandType::kSeek, position});
    for (auto& clock : clocks_) {
        clock->Reset(position);
    }
}

void MediaGraph::SetPaused(bool paused) {
    for (auto* node : node_ptrs_) {
        node->SetPaused(paused);
    }
    for (auto& clock : clocks_) {
        clock->SetPaused(paused);
    }
}

void MediaGraph::ReportEvent(GraphEvent event) {
    if (event == GraphEvent::kEos) {
        state_ = GraphState::kFinished;
    } else if (event == GraphEvent::kError) {
        state_ = GraphState::kError;
    }

    if (event_cb_) {
        event_cb_(event);
    }
}

}  // namespace mvp::graph
