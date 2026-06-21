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

bool VideoSinkNode::Configure(const NodeConfig& /*config*/) {
    state_ = NodeState::kConfigured;
    return true;
}

bool VideoSinkNode::Negotiate() {
    return true;
}

bool VideoSinkNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    if (state_ != NodeState::kConfigured) {
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
        // --- AudioMaster: sync video to audio clock ---

        // 1. Frame interval
        double delay = pts - last_pts;
        if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
            delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
        }

        // 2. Audio/video difference
        double diff = pts - audio_clock_->Get();

        // 3. Adaptive sync threshold
        double sync_threshold = std::clamp(delay, sync::kSyncThresholdMin,
                                           sync::kSyncThresholdMax);

        // 4. Correct delay
        if (diff > sync_threshold) {
            // Video ahead: slow down
            if (delay > sync::kSyncThresholdMax) {
                delay += diff;
            } else {
                delay = 2 * delay;
            }
        } else if (diff < -sync_threshold) {
            // Video behind: display immediately
            delay = 0.0;
        }

        // 5. Accumulate to absolute timeline
        frame_timer_ += delay;

        // 6. Compute actual wait from wall-clock
        double now = Clock::Now();
        double actual_wait = frame_timer_ - now;

        // 7. Reset on large discontinuity
        if (actual_wait < -sync::kMaxSleepSeconds) {
            frame_timer_ = now;
            return 0.0;
        }

        if (actual_wait > 0.0) {
            return std::min(actual_wait, sync::kMaxSleepSeconds);
        }
        return 0.0;
    }

    // --- VideoMaster: self-driven by frame interval ---
    double delay = pts - last_pts;
    if (delay <= sync::kFrameDelayMin || delay > sync::kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }
    double target_time = last_display_time + delay;
    double now = Clock::Now();
    double wait = target_time - now;
    if (wait > sync::kFrameDelayMin) {
        return std::min(wait, sync::kMaxSleepSeconds);
    }
    return 0.0;
}

void VideoSinkNode::RenderLoop() {
    double last_pts = 0.0;
    double last_display_time = Clock::Now();
    frame_timer_ = Clock::Now();

    while (running_.load(std::memory_order_relaxed)) {
        // Handle pause
        if (paused_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Pull next frame from input link
        auto opt_buf = input_port_->Pull();
        if (!opt_buf) {
            break;  // Link aborted
        }

        MediaBuffer& buf = *opt_buf;

        // EOS → report to graph and hold last frame
        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            if (graph_) {
                graph_->ReportEvent(GraphEvent::kEos);
            }
            // Hold last frame visible — wait for stop
            while (running_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            break;
        }

        if (!buf.IsFrame()) {
            continue;
        }

        MediaFrame& mf = buf.AsFrame();
        if (!mf.IsValid()) {
            continue;
        }

        double pts = mf.pts();

        // A/V sync timing
        double delay = ComputeDisplayDelay(pts, last_pts, last_display_time);
        if (delay > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(static_cast<int64_t>(delay * 1e6)));
        }

        last_display_time = Clock::Now();
        last_pts = pts;

        // Update video clock
        if (video_clock_) {
            video_clock_->Set(pts);
        }

        // Render the frame
        VideoFrame vf = MakeVideoFrame(mf);
        renderer_->Render(vf);

        // App callback
        if (frame_cb_) {
            frame_cb_(vf);
        }
    }
}

}  // namespace mvp::graph
