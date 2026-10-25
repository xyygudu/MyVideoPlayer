#include "nodes/frame_callback_sink_node.h"

#include <algorithm>
#include <optional>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"

namespace mvp::graph {

FrameCallbackSinkNode::FrameCallbackSinkNode(int max_width, Callback callback)
    : max_width_(std::max(16, max_width)),
      callback_(std::move(callback)),
      input_port_(std::make_unique<InputPort>(this)) {}

FrameCallbackSinkNode::~FrameCallbackSinkNode() { Stop(); }

void FrameCallbackSinkNode::DeclareCaps() {
    FormatCaps caps;
    caps.media_type = MediaType::kVideo;
    input_port_->SetCaps(caps);
}

bool FrameCallbackSinkNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("FrameCallbackSinkNode: input not connected");
        return false;
    }
    state_ = NodeState::kConfigured;
    return true;
}

bool FrameCallbackSinkNode::Prepare() {
    state_ = NodeState::kPrepared;
    return true;
}

bool FrameCallbackSinkNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("FrameCallbackSinkNode: Start called in invalid state {}", static_cast<int>(state_));
        return false;
    }
    running_ = true;
    thread_ = std::thread(&FrameCallbackSinkNode::DeliverLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void FrameCallbackSinkNode::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    sws_freeContext(sws_);
    sws_ = nullptr;
    state_ = NodeState::kIdle;
}

void FrameCallbackSinkNode::DeliverLoop() {
    CapturedFrame frame;
    while (running_.load(std::memory_order_relaxed)) {
        std::optional<MediaBuffer> buf = input_port_->Pull();
        if (!buf || HasFlag(buf->flags(), BufferFlags::kEos)) {
            return;
        }
        if (Convert(*buf, &frame) && callback_) {
            callback_(frame);
        }
    }
}

bool FrameCallbackSinkNode::Convert(const MediaBuffer& buf, CapturedFrame* out) {
    if (!buf.IsFrame() || !buf.AsFrame().IsValid() || buf.AsFrame().IsHardware()) {
        return false;
    }
    const AVFrame* src = buf.AsFrame().RawFrame();
    const double scale = std::min(1.0, static_cast<double>(max_width_) / src->width);
    const int width = std::max(2, static_cast<int>(src->width * scale) & ~1);
    const int height = std::max(2, static_cast<int>(src->height * scale) & ~1);
    sws_ = sws_getCachedContext(sws_, src->width, src->height, WithoutJpegRange(src->format), width, height,
                                AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) {
        SPDLOG_WARN("FrameCallbackSinkNode: no conversion from pixel format {}", src->format);
        return false;
    }
    out->width = width;
    out->height = height;
    out->pts = buf.timestamp().pts;
    out->rgba.resize(static_cast<size_t>(width) * height * 4);
    uint8_t* dst[4] = {out->rgba.data(), nullptr, nullptr, nullptr};
    const int dst_stride[4] = {width * 4, 0, 0, 0};
    sws_scale(sws_, src->data, src->linesize, 0, src->height, dst, dst_stride);
    return true;
}

}  // namespace mvp::graph
