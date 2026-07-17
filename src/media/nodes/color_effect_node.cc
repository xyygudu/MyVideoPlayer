#include "nodes/color_effect_node.h"

#include <algorithm>

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

    MediaFrame mf = input.AsFrame().MakeWritable();
    if (!mf.IsValid()) { emit(std::move(input)); return; }

    if (!IsPlanarYuvPixelFormat(mf.format())) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN("ColorEffectNode: unsupported pixel format {}, passing through", mf.format());
            logged_unsupported_format_ = true;
        }
        emit(std::move(input));
        return;
    }

    auto lut = pixel_ops::BuildColorLut(b, c, s);
    pixel_ops::ApplyLut(mf.PlaneData(0), mf.PlaneLinesize(0), mf.width(), mf.height(), lut.y);

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(mf.format(), mf.width(), mf.height());
    int row_bytes = layout.interleaved ? layout.width * 2 : layout.width;
    int plane_count = layout.interleaved ? 1 : 2;
    for (int p = 0; p < plane_count; ++p) {
        pixel_ops::ApplyLut(mf.PlaneData(1 + p), mf.PlaneLinesize(1 + p),
                            layout.width, layout.height, lut.uv);
    }

    MediaBuffer out(std::move(mf), input.timestamp(), input.flags());
    out.set_serial(input.serial());
    emit(std::move(out));
}

}  // namespace mvp::graph
