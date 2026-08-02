#include "nodes/video_sink_node.h"

#include <algorithm>
#include <chrono>

#include <spdlog/spdlog.h>

#include "clock.h"
#include "graph/graph_command.h"
#include "sync_constants.h"
#include "video_renderer.h"
#include "video_sink_node.h"

namespace mvp::graph {
namespace {
// Video only serves as the reference when nothing better is offered: its
// pacing is driven by this node's own sleeps, not by an external device.
constexpr int kClockPriority = 10;
}  // namespace

VideoSinkNode::VideoSinkNode() {
    input_port_ = std::make_unique<InputPort>(this);
}

VideoSinkNode::~VideoSinkNode() { Stop(); }

ClockOffer VideoSinkNode::ProvideClock() {
    return {clock_, kClockPriority};
}

bool VideoSinkNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("VideoSinkNode: input port not connected");
        return false;
    }
    const MediaFormat& fmt = input_port_->Format();
    if (!fmt.IsVideo()) {
        SPDLOG_ERROR("VideoSinkNode: input format is not video");
        return false;
    }
    const Rational& fr = fmt.AsVideo().frame_rate;
    if (fr.num > 0 && fr.den > 0) {
        video_fps_ = static_cast<double>(fr.num) / fr.den;
    }
    master_clock_ = graph_ ? graph_->MasterClock() : nullptr;
    return true;
}

bool VideoSinkNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    if (state_ != NodeState::kIdle && state_ != NodeState::kConfigured) {
        SPDLOG_ERROR("VideoSinkNode: Prepare in invalid state");
        return false;
    }
    if (!renderer_) {
        SPDLOG_ERROR("VideoSinkNode: no renderer set");
        state_ = NodeState::kError;
        return false;
    }
    state_ = NodeState::kPrepared;
    return true;
}

bool VideoSinkNode::Start() {
    if (state_ != NodeState::kPrepared) {
        return false;
    }
    running_ = true;
    paused_ = false;
    render_thread_ = std::thread(&VideoSinkNode::RenderLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void VideoSinkNode::Stop() {
    if (state_ != NodeState::kRunning && state_ != NodeState::kPaused) {
        return;
    }
    running_ = false;
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
    state_ = NodeState::kIdle;
}

void VideoSinkNode::Flush() {
    // Reset sync timeline — will be re-initialized on next frame.
    frame_timer_ = 0.0;
}

void VideoSinkNode::OnCommand(const Command& cmd) {
    if (cmd.type == CommandType::kSeek) {
        step_.store(true, std::memory_order_relaxed);
    } else if (cmd.type == CommandType::kRedraw) {
        redraw_.store(true, std::memory_order_relaxed);
    }
}


void VideoSinkNode::SetPaused(bool paused) {
    paused_ = paused;
    if (!paused) {
        step_.store(false, std::memory_order_relaxed);
    }
}
void VideoSinkNode::SetRenderer(mvp::VideoRenderer* renderer) {
    renderer_ = renderer;
}

std::vector<InputPort*> VideoSinkNode::Inputs() {
    return {input_port_.get()};
}

double VideoSinkNode::ComputeDisplayDelay(double pts, double last_pts,
                                          double last_display_time) {
    // An external reference exists only when someone else won arbitration.
    if (master_clock_ && master_clock_ != clock_.get()) {
        return ComputeSlavedDelay(pts, last_pts);
    }
    return ComputeFreeRunDelay(pts, last_pts, last_display_time);
}

double VideoSinkNode::ComputeSlavedDelay(double pts, double last_pts) {
    // 1. Frame interval
    double delay = pts - last_pts;
    if (delay <= kFrameDelayMin || delay > kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }

    // 2. Master clock difference + 3. adaptive sync threshold
    double diff = pts - master_clock_->Get();
    double sync_threshold =
        std::clamp(delay, kSyncThresholdMin, kSyncThresholdMax);

    // 4. Correct delay
    if (diff > sync_threshold) {
        delay = (delay > kSyncThresholdMax) ? (delay + diff) : (2 * delay);
    } else if (diff < -sync_threshold) {
        delay = 0.0;  // Video behind: display immediately
    }

    // 5. Accumulate to absolute timeline; 6. compute wall-clock wait
    frame_timer_ += delay;
    double now = Clock::Now();
    double actual_wait = frame_timer_ - now;

    // 7. Reset on large discontinuity
    if (actual_wait < -kMaxSleepSeconds) {
        frame_timer_ = now;
        return 0.0;
    }
    return (actual_wait > 0.0) ? std::min(actual_wait, kMaxSleepSeconds)
                               : 0.0;
}

double VideoSinkNode::ComputeFreeRunDelay(double pts, double last_pts,
                                          double last_display_time) {
    double delay = pts - last_pts;
    if (delay <= kFrameDelayMin || delay > kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }
    double wait = (last_display_time + delay) - Clock::Now();
    return (wait > kFrameDelayMin)
               ? std::min(wait, kMaxSleepSeconds)
               : 0.0;
}

void VideoSinkNode::SyncAndRender(MediaFrame mf, double& last_pts,
                                  double& last_display_time) {
    double pts = mf.pts();

    double delay = ComputeDisplayDelay(pts, last_pts, last_display_time);
    if (delay > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int64_t>(delay * 1e6)));
    }

    last_display_time = Clock::Now();
    last_pts = pts;
    RenderFrame(std::move(mf));
}

void VideoSinkNode::RenderFrame(MediaFrame frame) {
    clock_->Set(frame.pts());
    current_frame_ = std::move(frame);
    renderer_->Render(current_frame_);
}

void VideoSinkNode::RedrawCurrent() {
    if (current_frame_.IsValid()) {
        renderer_->Render(current_frame_);
    }
}

void VideoSinkNode::RenderLoop() {
    double last_pts = 0.0;
    double last_display_time = Clock::Now();
    frame_timer_ = Clock::Now();

    while (running_.load(std::memory_order_relaxed)) {
        bool paused = paused_.load(std::memory_order_relaxed);
        if (paused && !step_.load(std::memory_order_relaxed)) {
            if (redraw_.exchange(false, std::memory_order_relaxed)) {
                RedrawCurrent();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Pull already dropped stale and malformed buffers.
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            if (graph_) {
                graph_->ReportEvent(GraphEvent::kEos);
            }
            continue;
        }
        if (!buf.IsFrame()) {
            SPDLOG_WARN("VideoSinkNode: unexpected non-frame payload");
            continue;
        }

        if (paused_.load(std::memory_order_relaxed)) {
            last_pts = buf.AsFrame().pts();
            last_display_time = Clock::Now();
            RenderFrame(std::move(buf.AsFrame()));
            step_.store(false, std::memory_order_relaxed);
        } else {
            SyncAndRender(std::move(buf.AsFrame()), last_pts, last_display_time);
        }
    }
}

}  // namespace mvp::graph
