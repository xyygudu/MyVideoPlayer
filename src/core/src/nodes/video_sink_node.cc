#include "nodes/video_sink_node.h"

#include <algorithm>
#include <chrono>

#include <spdlog/spdlog.h>

#include "clock.h"
#include "frame_impl.h"
#include "sync_constants.h"
#include "video_renderer.h"

namespace mvp::graph {

VideoSinkNode::VideoSinkNode() {
    input_port_ = std::make_unique<InputPort>(this);
}

VideoSinkNode::~VideoSinkNode() { Stop(); }

bool VideoSinkNode::Negotiate() {
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

void VideoSinkNode::SetRenderer(mvp::VideoRenderer* renderer) {
    renderer_ = renderer;
}

void VideoSinkNode::SetAudioClock(mvp::Clock* audio_clock) {
    audio_clock_ = audio_clock;
}

void VideoSinkNode::SetVideoClock(mvp::Clock* video_clock) {
    video_clock_ = video_clock;
}

std::vector<InputPort*> VideoSinkNode::Inputs() {
    return {input_port_.get()};
}

double VideoSinkNode::ComputeDisplayDelay(double pts, double last_pts,
                                          double last_display_time) {
    if (sync_mode_ == SyncMode::kAudioMaster && audio_clock_) {
        return ComputeAudioMasterDelay(pts, last_pts);
    }
    return ComputeVideoMasterDelay(pts, last_pts, last_display_time);
}

double VideoSinkNode::ComputeAudioMasterDelay(double pts, double last_pts) {
    // 1. Frame interval
    double delay = pts - last_pts;
    if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }

    // 2. Audio/video difference + 3. adaptive sync threshold
    double diff = pts - audio_clock_->Get();
    double sync_threshold =
        std::clamp(delay, sync::kSyncThresholdMin, sync::kSyncThresholdMax);

    // 4. Correct delay
    if (diff > sync_threshold) {
        delay = (delay > sync::kSyncThresholdMax) ? (delay + diff) : (2 * delay);
    } else if (diff < -sync_threshold) {
        delay = 0.0;  // Video behind: display immediately
    }

    // 5. Accumulate to absolute timeline; 6. compute wall-clock wait
    frame_timer_ += delay;
    double now = Clock::Now();
    double actual_wait = frame_timer_ - now;

    // 7. Reset on large discontinuity
    if (actual_wait < -sync::kMaxSleepSeconds) {
        frame_timer_ = now;
        return 0.0;
    }
    return (actual_wait > 0.0) ? std::min(actual_wait, sync::kMaxSleepSeconds)
                               : 0.0;
}

double VideoSinkNode::ComputeVideoMasterDelay(double pts, double last_pts,
                                              double last_display_time) {
    double delay = pts - last_pts;
    if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }
    double wait = (last_display_time + delay) - Clock::Now();
    return (wait > sync::kFrameDelayMin)
               ? std::min(wait, sync::kMaxSleepSeconds)
               : 0.0;
}

void VideoSinkNode::HoldLastFrameUntilStop() {
    // After EOS, keep the last frame visible until the node is stopped.
    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void VideoSinkNode::SyncAndRender(MediaFrame& mf, double& last_pts,
                                  double& last_display_time) {
    double pts = mf.pts();

    double delay = ComputeDisplayDelay(pts, last_pts, last_display_time);
    if (delay > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int64_t>(delay * 1e6)));
    }

    last_display_time = Clock::Now();
    last_pts = pts;
    if (video_clock_) {
        video_clock_->Set(pts);
    }

    VideoFrame vf = MakeVideoFrame(mf);
    renderer_->Render(vf);
    if (frame_cb_) {
        frame_cb_(vf);
    }
}

void VideoSinkNode::RenderLoop() {
    double last_pts = 0.0;
    double last_display_time = Clock::Now();
    frame_timer_ = Clock::Now();

    while (running_.load(std::memory_order_relaxed)) {
        if (paused_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            if (graph_) {
                graph_->ReportEvent(GraphEvent::kEos);
            }
            HoldLastFrameUntilStop();
            break;
        }

        if (!buf.IsFrame()) {
            continue;
        }
        MediaFrame& mf = buf.AsFrame();
        if (!mf.IsValid()) {
            continue;
        }

        SyncAndRender(mf, last_pts, last_display_time);
    }
}

}  // namespace mvp::graph
