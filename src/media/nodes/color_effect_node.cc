#include "nodes/color_effect_node.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "media_frame.h"

namespace mvp::graph {

namespace {

/// Applies `(v - 128) * scale + 128 + offset`, clamped to [0, 255].
inline uint8_t ApplyLinear(uint8_t v, float scale, float offset) {
    float result = (static_cast<float>(v) - 128.0f) * scale + 128.0f + offset;
    return static_cast<uint8_t>(std::clamp(result, 0.0f, 255.0f) + 0.5f);
}

}  // namespace

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
    // Color adjustment does not change resolution or pixel format.
    output_port_->SetFormat(input_port_->Format());
    return true;
}

bool ColorEffectNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    state_ = NodeState::kPrepared;
    return true;
}

bool ColorEffectNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("ColorEffectNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }
    state_ = NodeState::kRunning;
    return true;
}

void ColorEffectNode::Stop() {
    state_ = NodeState::kIdle;
}

void ColorEffectNode::Flush() {
    // No internal buffering; parameters persist across seeks.
}

std::vector<EffectParam> ColorEffectNode::Params() const {
    return {
        {"brightness", "亮度", EffectParamType::kFloat, brightness_.load(),
         0.0f, -1.0f, 1.0f, {}},
        {"contrast", "对比度", EffectParamType::kFloat, contrast_.load(),
         1.0f, 0.0f, 3.0f, {}},
        {"saturation", "饱和度", EffectParamType::kFloat, saturation_.load(),
         1.0f, 0.0f, 3.0f, {}},
    };
}

void ColorEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    if (!std::holds_alternative<float>(value)) {
        SPDLOG_WARN("ColorEffectNode: param '{}' expects a float value", id);
        return;
    }
    if (id == "brightness") {
        brightness_.store(value);
    } else if (id == "contrast") {
        contrast_.store(value);
    } else if (id == "saturation") {
        saturation_.store(value);
    } else {
        SPDLOG_WARN("ColorEffectNode: unknown param id '{}'", id);
    }
}

void ColorEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    if (!enabled_.load() || !input.IsFrame()) {
        emit(std::move(input));
        return;
    }

    AVFrame* frame = input.AsFrame().RawFrame();
    if (!frame || !frame->data[0]) {
        emit(std::move(input));
        return;
    }

    if (!IsPlanarYuvPixelFormat(frame->format)) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN(
                "ColorEffectNode: unsupported pixel format {}, passing "
                "frames through unmodified",
                frame->format);
            logged_unsupported_format_ = true;
        }
        emit(std::move(input));
        return;
    }

    if (av_frame_make_writable(frame) < 0) {
        SPDLOG_WARN("ColorEffectNode: frame not writable, passing through");
        emit(std::move(input));
        return;
    }

    // Snapshot all parameters once per frame — not inside the pixel loop.
    const float brightness_offset = std::get<float>(brightness_.load()) * 255.0f;
    const float contrast = std::get<float>(contrast_.load());
    const float saturation = std::get<float>(saturation_.load());

    for (int y = 0; y < frame->height; ++y) {
        uint8_t* row = frame->data[0] + static_cast<ptrdiff_t>(y) * frame->linesize[0];
        for (int x = 0; x < frame->width; ++x) {
            row[x] = ApplyLinear(row[x], contrast, brightness_offset);
        }
    }

    ChromaPlaneLayout layout =
        ComputeChromaPlaneLayout(frame->format, frame->width, frame->height);
    int chroma_row_bytes = layout.interleaved ? layout.width * 2 : layout.width;
    int chroma_plane_count = layout.interleaved ? 1 : 2;
    for (int plane = 0; plane < chroma_plane_count; ++plane) {
        uint8_t* base = frame->data[1 + plane];
        int linesize = frame->linesize[1 + plane];
        for (int y = 0; y < layout.height; ++y) {
            uint8_t* row = base + static_cast<ptrdiff_t>(y) * linesize;
            for (int x = 0; x < chroma_row_bytes; ++x) {
                row[x] = ApplyLinear(row[x], saturation, 0.0f);
            }
        }
    }

    emit(std::move(input));
}

}  // namespace mvp::graph
