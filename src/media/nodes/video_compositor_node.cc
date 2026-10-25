#include "nodes/video_compositor_node.h"

#include <algorithm>
#include <cmath>
#include <optional>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#include <spdlog/spdlog.h>

#include "video/pixel_ops.h"

namespace mvp::graph {
namespace {

constexpr double kOverlayTimeout = 1.0;  // Seconds without a new overlay frame before hiding it
constexpr int kMinOverlayWidth = 32;
constexpr int kOutlineThickness = 2;
constexpr uint32_t kOutlineColor = 0xFFFFFF;

int EvenFloor(double value) { return static_cast<int>(value) & ~1; }

}  // namespace

VideoCompositorNode::VideoCompositorNode(CameraOverlayOptions options)
    : options_(std::move(options)),
      main_input_(std::make_unique<InputPort>(this)),
      overlay_input_(std::make_unique<InputPort>(this)),
      output_port_(std::make_unique<OutputPort>(this)) {}

VideoCompositorNode::~VideoCompositorNode() { Stop(); }

void VideoCompositorNode::DeclareCaps() {
    FormatCaps caps;
    caps.media_type = MediaType::kVideo;
    caps.pixel_formats = {PixelFormat::kRGB32};
    main_input_->SetCaps(caps);
    overlay_input_->SetCaps(caps);
    output_port_->SetCaps(caps);
}

bool VideoCompositorNode::Negotiate() {
    if (!main_input_->IsConnected() || !overlay_input_->IsConnected()) {
        SPDLOG_ERROR("VideoCompositorNode: both inputs must be connected");
        return false;
    }
    const MediaFormat& main = main_input_->Format();
    if (!main.IsVideo() || main.AsVideo().pixel_format != PixelFormat::kRGB32) {
        SPDLOG_ERROR("VideoCompositorNode: the main input is not BGRA video");
        return false;
    }
    canvas_width_ = main.AsVideo().width;
    canvas_height_ = main.AsVideo().height;
    output_port_->SetFormat(main);
    state_ = NodeState::kConfigured;
    return true;
}

bool VideoCompositorNode::Prepare() {
    overlay_ = MediaFrame();
    overlay_ended_ = false;
    state_ = NodeState::kPrepared;
    return true;
}

bool VideoCompositorNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("VideoCompositorNode: Start called in invalid state {}", static_cast<int>(state_));
        return false;
    }
    running_ = true;
    thread_ = std::thread(&VideoCompositorNode::ComposeLoop, this);
    state_ = NodeState::kRunning;
    return true;
}

void VideoCompositorNode::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    sws_freeContext(sws_);
    sws_ = nullptr;
    state_ = NodeState::kIdle;
}

void VideoCompositorNode::ComposeLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        std::optional<MediaBuffer> buf = main_input_->Pull();
        if (!buf) {
            return;  // Link aborted: the graph is stopping
        }
        if (HasFlag(buf->flags(), BufferFlags::kEos)) {
            output_port_->Push(std::move(*buf));
            return;
        }
        if (!buf->IsFrame()) {
            continue;
        }
        DrainOverlay();
        // The screen frame is referenced by this buffer only: no copy.
        MediaFrame canvas = std::move(buf->AsFrame()).MakeWritable();
        DrawOverlay(canvas, buf->timestamp().pts);
        MediaBuffer out(std::move(canvas), buf->timestamp(), buf->flags());
        out.set_serial(buf->serial());
        if (!output_port_->Push(std::move(out))) {
            return;
        }
    }
}

void VideoCompositorNode::DrainOverlay() {
    std::optional<MediaBuffer> newest;
    while (!overlay_ended_) {
        std::optional<MediaBuffer> buf = overlay_input_->TryPull();
        if (!buf) {
            break;
        }
        if (HasFlag(buf->flags(), BufferFlags::kEos)) {
            overlay_ended_ = true;
        } else if (buf->IsFrame()) {
            newest = std::move(buf);
        }
    }
    if (newest) {
        ScaleOverlay(newest->AsFrame());
        overlay_pts_ = newest->timestamp().pts;
    }
}

VideoCompositorNode::Placement VideoCompositorNode::PlaceOverlay(int source_width, int source_height) const {
    Placement place;
    const int max_width = canvas_width_ & ~1;
    place.width = std::clamp(EvenFloor(canvas_width_ * options_.width_ratio), std::min(kMinOverlayWidth, max_width),
                             max_width);
    place.height = std::min(EvenFloor(static_cast<double>(place.width) * source_height / source_width),
                            canvas_height_ & ~1);
    const int margin = std::max(8, canvas_width_ / 50);
    const bool left = options_.corner == OverlayCorner::kTopLeft || options_.corner == OverlayCorner::kBottomLeft;
    const bool top = options_.corner == OverlayCorner::kTopLeft || options_.corner == OverlayCorner::kTopRight;
    place.x = left ? margin : canvas_width_ - place.width - margin;
    place.y = top ? margin : canvas_height_ - place.height - margin;
    return place;
}

void VideoCompositorNode::ScaleOverlay(const MediaFrame& source) {
    const Placement place = PlaceOverlay(source.width(), source.height());
    if (place.width <= 0 || place.height <= 0) {
        return;
    }
    sws_ = sws_getCachedContext(sws_, source.width(), source.height(), AV_PIX_FMT_BGRA, place.width, place.height,
                                AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    MediaFrame scaled = sws_ ? overlay_pool_.Acquire(place.width, place.height, AV_PIX_FMT_BGRA) : MediaFrame();
    if (!scaled.IsValid()) {
        SPDLOG_WARN("VideoCompositorNode: cannot scale the overlay");
        return;
    }
    const AVFrame* src = source.RawFrame();
    AVFrame* dst = scaled.RawFrame();
    sws_scale(sws_, src->data, src->linesize, 0, source.height(), dst->data, dst->linesize);
    overlay_ = std::move(scaled);
    overlay_place_ = place;
}

void VideoCompositorNode::DrawOverlay(MediaFrame& canvas, double pts) {
    if (!overlay_.IsValid() || pts - overlay_pts_ > kOverlayTimeout) {
        return;
    }
    const Placement& place = overlay_place_;
    const pixel_ops::BgraImage image{canvas.PlaneData(0), canvas.width(), canvas.height(), canvas.PlaneLinesize(0)};
    pixel_ops::BlitBgra(overlay_.PlaneData(0), overlay_.PlaneLinesize(0), overlay_.width(), overlay_.height(), image,
                        place.x, place.y, options_.mirror);
    pixel_ops::OutlineRectBgra(image, place.x - kOutlineThickness, place.y - kOutlineThickness,
                               place.width + 2 * kOutlineThickness, place.height + 2 * kOutlineThickness,
                               kOutlineThickness, kOutlineColor);
}

}  // namespace mvp::graph
