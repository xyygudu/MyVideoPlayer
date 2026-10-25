#include "nodes/video_sink_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <spdlog/spdlog.h>

#include "clock.h"
#include "graph/graph_command.h"
#include "sync_constants.h"
#include "video_renderer.h"

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

void VideoSinkNode::DeclareCaps() {
    FormatCaps caps;
    caps.media_type = MediaType::kVideo;
    caps.pixel_formats = {PixelFormat::kYUV420P, PixelFormat::kYUV422P,
                          PixelFormat::kYUV444P, PixelFormat::kNV12,
                          PixelFormat::kRGB32};
    // The renderer's backend decides whether hardware frames can be
    // presented zero-copy; without binding support the chain stays software.
    if (renderer_ &&
        renderer_->BindableHardwareDomain() != PixelFormat::kUnknown) {
        caps.pixel_formats.push_back(renderer_->BindableHardwareDomain());
    }
    input_port_->SetCaps(std::move(caps));
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
    // Nothing to do here: the render thread restarts its timeline itself when
    // the first frame of the new epoch arrives (see RenderLoop).
}

void VideoSinkNode::OnCommand(const Command& cmd) {
    if (cmd.type == CommandType::kSeek) {
        // The graph bumped the epoch before dispatching the command.
        preview_epoch_.store(graph_ ? graph_->SeekEpoch() : 0, std::memory_order_relaxed);
    } else if (cmd.type == CommandType::kResize) {
        // Called on the control thread; the renderer belongs to the render
        // thread, so only stash the target here.
        const uint64_t packed = (static_cast<uint64_t>(cmd.width) << 32) |
                                static_cast<uint32_t>(cmd.height);
        pending_size_.store(packed, std::memory_order_relaxed);
    }
}

void VideoSinkNode::SetPaused(bool paused) {
    paused_ = paused;
    if (!paused) {
        preview_epoch_.store(kNoPreview, std::memory_order_relaxed);
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
    // 1. Duration of the frame on screen
    double delay = pts - last_pts;
    if (delay <= kFrameDelayMin || delay > kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }

    // 2. Video clock (the frame on screen, extrapolated) vs master, exactly
    // like ffplay's compute_target_delay; 3. adaptive sync threshold
    const double diff = clock_->Get() - master_clock_->Get();
    const double sync_threshold = std::clamp(delay, kSyncThresholdMin, kSyncThresholdMax);

    // 4. Correct the delay unless the gap is a timestamp jump
    if (std::abs(diff) < kMaxFrameDuration) {
        if (diff <= -sync_threshold) {
            delay = std::max(0.0, delay + diff);  // Video behind: catch up
        } else if (diff >= sync_threshold && delay > kSyncThresholdMax) {
            delay = delay + diff;  // Long frames: wait out the lead at once
        } else if (diff >= sync_threshold) {
            delay = 2 * delay;  // Video ahead: repeat the current frame
        }
    }

    // 5. Accumulate to absolute timeline; 6. compute wall-clock wait
    frame_timer_ += delay;
    const double now = Clock::Now();
    const double actual_wait = frame_timer_ - now;

    // 7. Reset on large discontinuity
    if (actual_wait < -kMaxSleepSeconds) {
        frame_timer_ = now;
        return 0.0;
    }
    return std::max(actual_wait, 0.0);
}

double VideoSinkNode::ComputeFreeRunDelay(double pts, double last_pts,
                                          double last_display_time) {
    double delay = pts - last_pts;
    if (delay <= kFrameDelayMin || delay > kFrameDelayMax) {
        delay = (video_fps_ > 0) ? (1.0 / video_fps_) : 0.04;
    }
    double wait = (last_display_time + delay) - Clock::Now();
    return (wait > kFrameDelayMin) ? wait : 0.0;
}

void VideoSinkNode::SyncAndRender(MediaBuffer buf, Timeline& timeline) {
    if (timeline.fresh) {
        // First frame of an epoch (start or seek): show it at once and anchor
        // the frame timer to it. Intervals measured against the previous
        // epoch are meaningless (ffplay resets frame_timer on serial change).
        frame_timer_ = Clock::Now();
        PresentNow(std::move(buf), timeline);
        return;
    }
    const double delay = ComputeDisplayDelay(buf.timestamp().pts, timeline.last_pts,
                                             timeline.last_display_time);
    switch (WaitForSlot(delay, buf.serial())) {
        case SlotWait::kReached:
            PresentNow(std::move(buf), timeline);
            break;
        case SlotWait::kPaused:
            timeline.pending = std::move(buf);  // Shown once playback resumes.
            break;
        case SlotWait::kAbandoned:
            break;  // Seek or stop: the frame is stale.
    }
}

VideoSinkNode::SlotWait VideoSinkNode::WaitForSlot(double delay, int serial) {
    const double deadline = Clock::Now() + delay;
    for (;;) {
        if (!running_.load(std::memory_order_relaxed) || !IsCurrentEpoch(serial)) {
            return SlotWait::kAbandoned;
        }
        if (paused_.load(std::memory_order_relaxed)) {
            return SlotWait::kPaused;
        }
        const double remaining = deadline - Clock::Now();
        if (remaining <= 0.0) {
            return SlotWait::kReached;
        }
        std::this_thread::sleep_for(
            std::chrono::duration<double>(std::min(remaining, kMaxSleepSeconds)));
        ApplyPendingResize();
    }
}

void VideoSinkNode::PresentNow(MediaBuffer buf, Timeline& timeline) {
    const int serial = buf.serial();
    if (!IsCurrentEpoch(serial)) {
        return;  // A seek landed while this frame waited for its slot.
    }
    const bool first_of_epoch = timeline.fresh;
    timeline.fresh = false;
    timeline.last_pts = buf.timestamp().pts;
    timeline.last_display_time = Clock::Now();
    clock_->Set(timeline.last_pts, serial);
    PresentFrame(std::move(buf.AsFrame()), timeline.last_pts, serial);
    // Consume the paused-seek preview only if it belongs to this epoch: a
    // newer seek's preview stays pending, and a preview consumed while
    // playing cannot make a later pause advance one extra frame.
    int expected = serial;
    preview_epoch_.compare_exchange_strong(expected, kNoPreview, std::memory_order_relaxed);
    if (first_of_epoch && graph_) {
        // The epoch reached the screen (GStreamer ASYNC_DONE, mpv playback-restart).
        graph_->Bus().Post({GraphMessageType::kFirstFrame, this, serial, {}, timeline.last_pts});
    }
}

void VideoSinkNode::PresentFrame(MediaFrame frame, double pts, int serial) {
    {
        std::lock_guard lock(displayed_mutex_);
        current_frame_ = std::move(frame);
        current_pts_ = pts;
        current_serial_ = serial;
    }
    frames_presented_.fetch_add(1, std::memory_order_relaxed);
    RenderCurrent(serial);
}

bool VideoSinkNode::DisplayedPosition(double* pts, int* serial) const {
    std::lock_guard lock(displayed_mutex_);
    if (!current_frame_.IsValid()) {
        return false;
    }
    *pts = current_pts_;
    *serial = current_serial_;
    return true;
}

MediaFrame VideoSinkNode::DisplayedFrame(double* pts) const {
    std::lock_guard lock(displayed_mutex_);
    *pts = current_pts_;
    return current_frame_.IsValid() ? MediaFrame(current_frame_.RawFrame()) : MediaFrame();
}

void VideoSinkNode::RedrawCurrent() {
    if (current_frame_.IsValid()) {
        RenderCurrent(current_serial_);
    }
}

void VideoSinkNode::RenderCurrent(int serial) {
    renderer_->Render(current_frame_);
    if (device_lost_reported_ || !renderer_->DeviceLost()) {
        return;
    }
    device_lost_reported_ = true;
    SPDLOG_ERROR("VideoSinkNode: GPU device lost, presentation stops until the pipeline is rebuilt");
    if (graph_) {
        graph_->Bus().Post({GraphMessageType::kDeviceLost, this, serial, "GPU device lost"});
    }
}

bool VideoSinkNode::IsCurrentEpoch(int serial) const {
    return !graph_ || serial == graph_->SeekEpoch();
}

void VideoSinkNode::ReportEos(int serial) {
    // The last frame was presented before EOS arrived (frames stay ordered).
    if (graph_ && IsCurrentEpoch(serial)) {
        graph_->Bus().Post({GraphMessageType::kEos, this, serial, {}});
    }
}

void VideoSinkNode::ApplyPendingResize() {
    const uint64_t packed = pending_size_.exchange(0, std::memory_order_relaxed);
    if (packed == 0) {
        return;
    }
    SPDLOG_DEBUG("VideoSinkNode: applying resize {}x{}",
                 static_cast<int>(packed >> 32),
                 static_cast<int>(packed & 0xffffffffu));
    renderer_->Resize(static_cast<int>(packed >> 32),
                      static_cast<int>(packed & 0xffffffffu));
    RedrawCurrent();
}

void VideoSinkNode::RenderLoop() {
    Timeline timeline;
    while (running_.load(std::memory_order_relaxed)) {
        ApplyPendingResize();

        const bool preview = preview_epoch_.load(std::memory_order_relaxed) != kNoPreview;
        if (paused_.load(std::memory_order_relaxed) && !preview) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Pull already dropped stale and malformed buffers.
        std::optional<MediaBuffer> opt_buf = NextBuffer(timeline);
        if (!opt_buf) {
            break;  // Link aborted
        }
        MediaBuffer& buf = *opt_buf;

        if (HasFlag(buf.flags(), BufferFlags::kEos)) {
            ReportEos(buf.serial());
            continue;
        }
        if (!buf.IsFrame()) {
            SPDLOG_WARN("VideoSinkNode: unexpected non-frame payload");
            continue;
        }
        if (buf.serial() != timeline.serial) {
            timeline = Timeline{buf.serial()};  // New epoch: restart pacing.
        }

        if (paused_.load(std::memory_order_relaxed)) {
            SPDLOG_DEBUG("VideoSinkNode: step present pts={:.3f}", buf.timestamp().pts);
            PresentNow(std::move(buf), timeline);
        } else {
            SyncAndRender(std::move(buf), timeline);
        }
    }
}

std::optional<MediaBuffer> VideoSinkNode::NextBuffer(Timeline& timeline) {
    if (timeline.pending) {
        std::optional<MediaBuffer> held = std::move(timeline.pending);
        timeline.pending.reset();
        // Paused here means a seek preview is due, which makes it stale too.
        if (IsCurrentEpoch(held->serial()) && !paused_.load(std::memory_order_relaxed)) {
            return held;
        }
    }
    return input_port_->Pull();
}

}  // namespace mvp::graph
