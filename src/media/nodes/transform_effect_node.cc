#include "nodes/transform_effect_node.h"

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

constexpr float kPi = 3.14159265358979323846f;

/// Inverse of the (rotate -> flip -> scale -> translate) affine transform,
/// evaluated in a single plane's own pixel coordinate space so chroma
/// planes (which use a smaller width/height than luma) get a proportionally
/// identical transform. See design.md for the derivation.
void InverseMap(const TransformAffineParams& p, float plane_width,
                float plane_height, float dst_x, float dst_y, float* src_x,
                float* src_y) {
    const float cx = plane_width * 0.5f;
    const float cy = plane_height * 0.5f;
    const float tx = p.translate_x * plane_width;
    const float ty = p.translate_y * plane_height;

    float ux = dst_x - cx - tx;
    float uy = dst_y - cy - ty;

    float sx = (p.scale_x != 0.0f) ? ux / p.scale_x : 0.0f;
    float sy = (p.scale_y != 0.0f) ? uy / p.scale_y : 0.0f;

    if (p.flip_h) sx = -sx;
    if (p.flip_v) sy = -sy;

    const float cos_t = std::cos(p.rotate_rad);
    const float sin_t = std::sin(p.rotate_rad);
    const float rx = sx * cos_t + sy * sin_t;
    const float ry = -sx * sin_t + sy * cos_t;

    *src_x = rx + cx;
    *src_y = ry + cy;
}

/// Bilinearly samples one component (a full plane for planar Y/U/V, or one
/// of the two interleaved components of an NV12 chroma plane) at a
/// fractional coordinate. Samples that fall outside [0,width)x[0,height)
/// use `fill` instead of reading out of bounds — this both implements the
/// "fill black at the edges" requirement and keeps the blend smooth for
/// coordinates that straddle the boundary.
uint8_t SampleComponent(const uint8_t* plane, int linesize, int width, int height,
                        int comp_stride, int comp_offset, float x, float y,
                        uint8_t fill) {
    auto texel = [&](int ix, int iy) -> float {
        if (ix < 0 || ix >= width || iy < 0 || iy >= height) {
            return static_cast<float>(fill);
        }
        return static_cast<float>(
            plane[static_cast<ptrdiff_t>(iy) * linesize + ix * comp_stride + comp_offset]);
    };

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float p00 = texel(x0, y0);
    const float p10 = texel(x0 + 1, y0);
    const float p01 = texel(x0, y0 + 1);
    const float p11 = texel(x0 + 1, y0 + 1);

    const float blended = (1.0f - fx) * (1.0f - fy) * p00 + fx * (1.0f - fy) * p10 +
                          (1.0f - fx) * fy * p01 + fx * fy * p11;
    return static_cast<uint8_t>(std::clamp(blended, 0.0f, 255.0f) + 0.5f);
}

/// Remaps one component of a plane (see SampleComponent) from `src` into
/// `dst`. Both buffers must have the same width/height/comp_stride/offset —
/// they describe the same plane before/after the transform.
void RemapComponent(const uint8_t* src, int src_linesize, uint8_t* dst,
                    int dst_linesize, int width, int height, int comp_stride,
                    int comp_offset, uint8_t fill,
                    const TransformAffineParams& p) {
    for (int y = 0; y < height; ++y) {
        uint8_t* dst_row = dst + static_cast<ptrdiff_t>(y) * dst_linesize;
        for (int x = 0; x < width; ++x) {
            float src_x, src_y;
            InverseMap(p, static_cast<float>(width), static_cast<float>(height),
                       static_cast<float>(x), static_cast<float>(y), &src_x, &src_y);
            dst_row[x * comp_stride + comp_offset] = SampleComponent(
                src, src_linesize, width, height, comp_stride, comp_offset, src_x, src_y, fill);
        }
    }
}

}  // namespace

TransformEffectNode::TransformEffectNode() {
    input_port_ = std::make_unique<InputPort>(this);
    output_port_ = std::make_unique<OutputPort>(this);
}

TransformEffectNode::~TransformEffectNode() = default;

bool TransformEffectNode::Negotiate() {
    if (!input_port_->IsConnected()) {
        SPDLOG_ERROR("TransformEffectNode: input port not connected");
        return false;
    }
    // Geometry transforms never change resolution or pixel format — the
    // output canvas is always the same size as the input (see design.md).
    output_port_->SetFormat(input_port_->Format());
    return true;
}

bool TransformEffectNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) {
        return true;
    }
    state_ = NodeState::kPrepared;
    return true;
}

bool TransformEffectNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("TransformEffectNode: Start called in invalid state {}",
                     static_cast<int>(state_));
        return false;
    }
    state_ = NodeState::kRunning;
    return true;
}

void TransformEffectNode::Stop() {
    state_ = NodeState::kIdle;
}

void TransformEffectNode::Flush() {
    // No internal buffering; parameters persist across seeks.
}

std::vector<EffectParam> TransformEffectNode::Params() const {
    return {
        {"rotate_deg", "旋转角度", EffectParamType::kFloat, rotate_deg_.load(),
         0.0f, 0.0f, 360.0f, {}},
        {"flip_h", "水平翻转", EffectParamType::kBool, flip_h_.load(), false, {}, {}, {}},
        {"flip_v", "垂直翻转", EffectParamType::kBool, flip_v_.load(), false, {}, {}, {}},
        {"scale_x", "水平缩放", EffectParamType::kFloat, scale_x_.load(),
         1.0f, 0.1f, 5.0f, {}},
        {"scale_y", "垂直缩放", EffectParamType::kFloat, scale_y_.load(),
         1.0f, 0.1f, 5.0f, {}},
        {"translate_x", "水平平移", EffectParamType::kFloat, translate_x_.load(),
         0.0f, -1.0f, 1.0f, {}},
        {"translate_y", "垂直平移", EffectParamType::kFloat, translate_y_.load(),
         0.0f, -1.0f, 1.0f, {}},
    };
}

void TransformEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    if (id == "rotate_deg") {
        if (std::holds_alternative<float>(value)) rotate_deg_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'rotate_deg' expects a float value");
    } else if (id == "flip_h") {
        if (std::holds_alternative<bool>(value)) flip_h_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'flip_h' expects a bool value");
    } else if (id == "flip_v") {
        if (std::holds_alternative<bool>(value)) flip_v_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'flip_v' expects a bool value");
    } else if (id == "scale_x") {
        if (std::holds_alternative<float>(value)) scale_x_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'scale_x' expects a float value");
    } else if (id == "scale_y") {
        if (std::holds_alternative<float>(value)) scale_y_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'scale_y' expects a float value");
    } else if (id == "translate_x") {
        if (std::holds_alternative<float>(value)) translate_x_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'translate_x' expects a float value");
    } else if (id == "translate_y") {
        if (std::holds_alternative<float>(value)) translate_y_.store(value);
        else SPDLOG_WARN("TransformEffectNode: param 'translate_y' expects a float value");
    } else {
        SPDLOG_WARN("TransformEffectNode: unknown param id '{}'", id);
    }
}

TransformAffineParams TransformEffectNode::SnapshotParams() const {
    TransformAffineParams p;
    p.rotate_rad = std::get<float>(rotate_deg_.load()) * kPi / 180.0f;
    p.flip_h = std::get<bool>(flip_h_.load());
    p.flip_v = std::get<bool>(flip_v_.load());
    p.scale_x = std::get<float>(scale_x_.load());
    p.scale_y = std::get<float>(scale_y_.load());
    p.translate_x = std::get<float>(translate_x_.load());
    p.translate_y = std::get<float>(translate_y_.load());
    return p;
}

bool TransformEffectNode::AllocateOutputFrame(const AVFrame* src, AVFrame* dst) const {
    dst->format = src->format;
    dst->width = src->width;
    dst->height = src->height;
    return av_frame_get_buffer(dst, 0) >= 0;
}

void TransformEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    if (!enabled_.load() || !input.IsFrame()) {
        emit(std::move(input));
        return;
    }

    const AVFrame* src = input.AsFrame().RawFrame();
    if (!src || !src->data[0]) {
        emit(std::move(input));
        return;
    }

    if (!IsPlanarYuvPixelFormat(src->format)) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN(
                "TransformEffectNode: unsupported pixel format {}, passing "
                "frames through unmodified",
                src->format);
            logged_unsupported_format_ = true;
        }
        emit(std::move(input));
        return;
    }

    AVFramePtr out_frame;
    if (!AllocateOutputFrame(src, out_frame.get())) {
        SPDLOG_WARN("TransformEffectNode: failed to allocate output frame, passing through");
        emit(std::move(input));
        return;
    }

    const TransformAffineParams params = SnapshotParams();

    RemapComponent(src->data[0], src->linesize[0], out_frame->data[0],
                   out_frame->linesize[0], src->width, src->height,
                   /*comp_stride=*/1, /*comp_offset=*/0, /*fill=*/0, params);

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(src->format, src->width, src->height);
    if (layout.interleaved) {
        RemapComponent(src->data[1], src->linesize[1], out_frame->data[1],
                       out_frame->linesize[1], layout.width, layout.height,
                       /*comp_stride=*/2, /*comp_offset=*/0, /*fill=*/128, params);
        RemapComponent(src->data[1], src->linesize[1], out_frame->data[1],
                       out_frame->linesize[1], layout.width, layout.height,
                       /*comp_stride=*/2, /*comp_offset=*/1, /*fill=*/128, params);
    } else {
        RemapComponent(src->data[1], src->linesize[1], out_frame->data[1],
                       out_frame->linesize[1], layout.width, layout.height,
                       /*comp_stride=*/1, /*comp_offset=*/0, /*fill=*/128, params);
        RemapComponent(src->data[2], src->linesize[2], out_frame->data[2],
                       out_frame->linesize[2], layout.width, layout.height,
                       /*comp_stride=*/1, /*comp_offset=*/0, /*fill=*/128, params);
    }

    MediaFrame out_media_frame(out_frame.get(), input.AsFrame().pts(), input.AsFrame().type());
    MediaBuffer out_buf(std::move(out_media_frame), input.timestamp(), input.flags());
    out_buf.set_serial(input.serial());
    emit(std::move(out_buf));
}

}  // namespace mvp::graph
