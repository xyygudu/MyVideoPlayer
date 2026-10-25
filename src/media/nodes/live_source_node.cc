#include "nodes/live_source_node.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include "graph/graph_command.h"
#include "graph/media_graph.h"

namespace mvp::graph {
namespace {

// Live sources outrank clockless nodes. Their clocks are interchangeable
// system clocks, so they tie, and the graph picks the first in topo order.
constexpr int kLiveClockPriority = 50;

}  // namespace

LiveSourceNode::LiveSourceNode()
    : output_port_(std::make_unique<OutputPort>(this)), clock_(std::make_shared<Clock>()) {}

LiveSourceNode::~LiveSourceNode() {
    if (device_thread_.joinable()) {
        // The thread may be inside hooks of a destroyed derived object.
        SPDLOG_CRITICAL("LiveSourceNode: destroyed with its device thread running (missing Stop())");
        Stop();
    }
}

bool LiveSourceNode::Open() {
    if (device_thread_.joinable()) {
        return state_ != NodeState::kError;
    }
    {
        std::lock_guard lock(mutex_);
        open_result_ = OpenResult::kPending;
        start_requested_ = eos_requested_ = stop_requested_ = false;
    }
    downstream_gone_ = false;
    device_thread_ = std::thread(&LiveSourceNode::DeviceThread, this);

    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return open_result_ != OpenResult::kPending; });
    const bool opened = open_result_ == OpenResult::kOpened;
    lock.unlock();
    if (!opened) {
        device_thread_.join();
        state_ = NodeState::kError;
        return false;
    }
    FormatCaps caps;
    caps.media_type = format_.media_type();
    output_port_->SetCaps(std::move(caps));
    state_ = NodeState::kOpened;
    return true;
}

ClockOffer LiveSourceNode::ProvideClock() { return {clock_, kLiveClockPriority}; }

bool LiveSourceNode::Negotiate() {
    output_port_->SetFormat(format_);
    state_ = NodeState::kConfigured;
    return true;
}

bool LiveSourceNode::Prepare() {
    // Frozen at 0 until Start(): what is captured first is stamped 0.
    clock_->Reset(0.0, graph_ ? graph_->SeekEpoch() : 0);
    clock_->SetPaused(true);
    state_ = NodeState::kPrepared;
    return true;
}

bool LiveSourceNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("{}: Start called in invalid state {}", Name(), static_cast<int>(state_));
        return false;
    }
    clock_->SetPaused(false);
    {
        std::lock_guard lock(mutex_);
        start_requested_ = true;
    }
    cv_.notify_all();
    state_ = NodeState::kRunning;
    return true;
}

void LiveSourceNode::Stop() {
    {
        std::lock_guard lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (device_thread_.joinable()) {
        InterruptDevice();
        device_thread_.join();
    }
    if (state_ != NodeState::kError) {
        state_ = NodeState::kIdle;
    }
}

void LiveSourceNode::OnCommand(const Command& cmd) {
    if (cmd.type != CommandType::kEndOfStream) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        eos_requested_ = true;
    }
    cv_.notify_all();
    InterruptDevice();
}

void LiveSourceNode::SetPaused(bool paused) { paused_.store(paused, std::memory_order_release); }

void LiveSourceNode::DeviceThread() {
    MediaFormat format;
    const bool opened = OpenDevice(&format);
    {
        std::lock_guard lock(mutex_);
        format_ = std::move(format);
        open_result_ = opened ? OpenResult::kOpened : OpenResult::kFailed;
    }
    cv_.notify_all();
    if (opened && WaitForStart()) {
        while (Capturing()) {
            CaptureStep();
        }
        EndStream();
    }
    CloseDevice();
}

bool LiveSourceNode::WaitForStart() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return start_requested_ || stop_requested_; });
    return !stop_requested_;
}

void LiveSourceNode::EndStream() {
    bool send_eos = false;
    {
        std::lock_guard lock(mutex_);
        send_eos = eos_requested_ && !stop_requested_;
    }
    if (send_eos && !downstream_gone_.load(std::memory_order_acquire)) {
        SPDLOG_INFO("{}: end of stream", Name());
        output_port_->Push(MediaBuffer::MakeEos(graph_ ? graph_->SeekEpoch() : 0));
    }
}

IClock* LiveSourceNode::TimeBase() const {
    IClock* master = graph_ ? graph_->MasterClock() : nullptr;
    return master ? master : clock_.get();
}

double LiveSourceNode::Now() const { return TimeBase()->Get(); }

bool LiveSourceNode::Capturing() const {
    std::lock_guard lock(mutex_);
    return !eos_requested_ && !stop_requested_ && !downstream_gone_.load(std::memory_order_acquire);
}

bool LiveSourceNode::Emit(MediaBuffer buf) {
    if (Paused()) {
        return true;
    }
    buf.set_serial(graph_ ? graph_->SeekEpoch() : 0);
    if (!output_port_->Push(std::move(buf))) {
        downstream_gone_.store(true, std::memory_order_release);
        return false;
    }
    return true;
}

void LiveSourceNode::SleepFor(double seconds) {
    if (seconds <= 0.0) {
        return;
    }
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::duration<double>(seconds),
                 [this] { return eos_requested_ || stop_requested_; });
}

void LiveSourceNode::PostError(const std::string& detail) {
    SPDLOG_ERROR("{}: {}", Name(), detail);
    if (graph_) {
        graph_->Bus().Post({GraphMessageType::kError, this, graph_->SeekEpoch(), Name() + ": " + detail});
    }
}

}  // namespace mvp::graph
