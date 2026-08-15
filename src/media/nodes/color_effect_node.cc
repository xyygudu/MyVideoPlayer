#include "nodes/color_effect_node.h"

#include <algorithm>
#include <utility>

extern "C" {
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "media_frame.h"
#include "pixel_ops.h"

namespace mvp::graph {

ColorEffectNode::ColorEffectNode() {
    input_port_ = std::make_unique<InputPort>(this);
    output_port_ = std::make_unique<OutputPort>(this);
}

ColorEffectNode::~ColorEffectNode() = default;

void ColorEffectNode::DeclareCaps() {
    // Format-transparent node: relay the downstream constraint upstream so
    // the producer negotiates a format the whole remaining chain accepts.
    if (output_port_->IsConnected()) {
        input_port_->SetCaps(output_port_->Peer()->Caps());
    }
}

bool ColorEffectNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("ColorEffectNode: input port not connected");
        return false;
    }
    output_port_->SetFormat(input_port_->Format());
    return true;
}

bool ColorEffectNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) return true;
    state_ = NodeState::kPrepared;
    return true;
}

bool ColorEffectNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("ColorEffectNode: Start called in invalid state {}", static_cast<int>(state_));
        return false;
    }
    state_ = NodeState::kRunning;
    return true;
}

void ColorEffectNode::Stop() { state_ = NodeState::kIdle; }
void ColorEffectNode::Flush() {}

std::vector<EffectParam> ColorEffectNode::Params() const {
    return {
        {"brightness", "亮度", EffectParamType::kFloat, brightness_.load(), 0.0f, -1.0f, 1.0f, {}},
        {"contrast", "对比度", EffectParamType::kFloat, contrast_.load(), 1.0f, 0.0f, 3.0f, {}},
        {"saturation", "饱和度", EffectParamType::kFloat, saturation_.load(), 1.0f, 0.0f, 3.0f, {}},
    };
}

void ColorEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    if (!std::holds_alternative<float>(value)) {
        SPDLOG_WARN("ColorEffectNode: param '{}' expects float", id);
        return;
    }
    if (id == "brightness") brightness_.store(value);
    else if (id == "contrast") contrast_.store(value);
    else if (id == "saturation") saturation_.store(value);
    else SPDLOG_WARN("ColorEffectNode: unknown param '{}'", id);
}

void ColorEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    if (!enabled_.load() || !input.IsFrame()) { emit(std::move(input)); return; }

    float b = std::get<float>(brightness_.load());
    float c = std::get<float>(contrast_.load());
    float s = std::get<float>(saturation_.load());
    if (b == 0.0f && c == 1.0f && s == 1.0f) { emit(std::move(input)); return; }

    // CPU effects convert hardware frames at the node boundary (GPU→CPU);
    // the GPU engine of a later stage will replace this download.
    MediaFrame& src = input.AsFrame();
    bool from_hw = src.IsHardware();
    MediaFrame mf;
    if (from_hw) {
        mf = TransferToSoftware(src);
        if (!mf.IsValid()) {
            SPDLOG_WARN("ColorEffectNode: hw frame download failed");
            emit(std::move(input));
            return;
        }
    } else {
        mf = std::move(src).MakeWritable();
        if (!mf.IsValid()) { emit(std::move(input)); return; }
    }

    if (!IsPlanarYuvPixelFormat(mf.format())) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN("ColorEffectNode: unsupported pixel format {}, passing through",
                        mf.format());
            logged_unsupported_format_ = true;
        }
        if (from_hw) {
            emit(std::move(input));  // Keep the zero-copy hw frame untouched.
        } else {
            emit(MediaBuffer(std::move(mf), input.timestamp(), input.flags()));
        }
        return;
    }

    auto lut = pixel_ops::BuildColorLut(b, c, s);
    pixel_ops::ApplyLut(mf.PlaneData(0), mf.PlaneLinesize(0), mf.width(), mf.height(), lut.y);

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(mf.format(), mf.width(), mf.height());
    int plane_count = layout.interleaved ? 1 : 2;
    for (int p = 0; p < plane_count; ++p) {
        pixel_ops::ApplyLut(mf.PlaneData(1 + p), mf.PlaneLinesize(1 + p),
                            layout.width, layout.height, lut.uv);
    }

    MediaBuffer out(std::move(mf), input.timestamp(), input.flags());
    emit(std::move(out));
}

}  // namespace mvp::graph
