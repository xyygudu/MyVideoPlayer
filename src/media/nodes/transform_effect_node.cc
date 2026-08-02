#include "nodes/transform_effect_node.h"

extern "C" {
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "media_frame.h"
#include "pixel_ops.h"

namespace mvp::graph {

namespace {

constexpr float kPi = 3.14159265358979323846f;

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
    output_port_->SetFormat(input_port_->Format());
    return true;
}

bool TransformEffectNode::Prepare() {
    if (state_ == NodeState::kPrepared || state_ == NodeState::kRunning) return true;
    state_ = NodeState::kPrepared;
    return true;
}

bool TransformEffectNode::Start() {
    if (state_ != NodeState::kPrepared) {
        SPDLOG_ERROR("TransformEffectNode: Start called in invalid state {}", static_cast<int>(state_));
        return false;
    }
    state_ = NodeState::kRunning;
    return true;
}

void TransformEffectNode::Stop() { state_ = NodeState::kIdle; }
void TransformEffectNode::Flush() {}

std::vector<EffectParam> TransformEffectNode::Params() const {
    return {
        {"rotate_deg", "旋转角度", EffectParamType::kFloat, rotate_deg_.load(), 0.0f, 0.0f, 360.0f, {}},
        {"flip_h", "水平翻转", EffectParamType::kBool, flip_h_.load(), false, {}, {}, {}},
        {"flip_v", "垂直翻转", EffectParamType::kBool, flip_v_.load(), false, {}, {}, {}},
        {"scale_x", "水平缩放", EffectParamType::kFloat, scale_x_.load(), 1.0f, 0.1f, 5.0f, {}},
        {"scale_y", "垂直缩放", EffectParamType::kFloat, scale_y_.load(), 1.0f, 0.1f, 5.0f, {}},
        {"translate_x", "水平平移", EffectParamType::kFloat, translate_x_.load(), 0.0f, -1.0f, 1.0f, {}},
        {"translate_y", "垂直平移", EffectParamType::kFloat, translate_y_.load(), 0.0f, -1.0f, 1.0f, {}},
    };
}

void TransformEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    if (id == "rotate_deg") {
        if (std::holds_alternative<float>(value)) rotate_deg_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'rotate_deg' expects float");
    } else if (id == "flip_h") {
        if (std::holds_alternative<bool>(value)) flip_h_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'flip_h' expects bool");
    } else if (id == "flip_v") {
        if (std::holds_alternative<bool>(value)) flip_v_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'flip_v' expects bool");
    } else if (id == "scale_x") {
        if (std::holds_alternative<float>(value)) scale_x_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'scale_x' expects float");
    } else if (id == "scale_y") {
        if (std::holds_alternative<float>(value)) scale_y_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'scale_y' expects float");
    } else if (id == "translate_x") {
        if (std::holds_alternative<float>(value)) translate_x_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'translate_x' expects float");
    } else if (id == "translate_y") {
        if (std::holds_alternative<float>(value)) translate_y_.store(value);
        else SPDLOG_WARN("TransformEffectNode: 'translate_y' expects float");
    } else {
        SPDLOG_WARN("TransformEffectNode: unknown param '{}'", id);
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

void TransformEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    if (!enabled_.load() || !input.IsFrame()) { emit(std::move(input)); return; }

    const MediaFrame& src_mf = input.AsFrame();
    if (!src_mf.IsValid()) { emit(std::move(input)); return; }

    if (!IsPlanarYuvPixelFormat(src_mf.format())) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN("TransformEffectNode: unsupported pixel format {}, passing through",
                        src_mf.format());
            logged_unsupported_format_ = true;
        }
        emit(std::move(input));
        return;
    }

    const TransformAffineParams params = SnapshotParams();
    if (params.rotate_rad == 0.0f && !params.flip_h && !params.flip_v &&
        params.scale_x == 1.0f && params.scale_y == 1.0f &&
        params.translate_x == 0.0f && params.translate_y == 0.0f) {
        emit(std::move(input));
        return;
    }

    MediaFrame out_mf = output_pool_.Acquire(src_mf.width(), src_mf.height(), src_mf.format(),
                                             src_mf.pts());
    if (!out_mf.IsValid()) { emit(std::move(input)); return; }
    if (TryApplyPermute(src_mf, out_mf, input, emit)) return;
    ApplyBilinear(src_mf, out_mf, input, emit);
}

bool TransformEffectNode::TryApplyPermute(const MediaFrame& src, MediaFrame& dst,
                                          MediaBuffer& input, OutputCallback& emit) {
    const TransformAffineParams params = SnapshotParams();
    if (!pixel_ops::TryPermutePlane(src.PlaneData(0), src.PlaneLinesize(0),
                                    dst.PlaneData(0), dst.PlaneLinesize(0),
                                    src.width(), src.height(), 1, 0, params)) {
        return false;
    }

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(src.format(), src.width(), src.height());
    if (layout.interleaved) {
        pixel_ops::TryPermutePlane(src.PlaneData(1), src.PlaneLinesize(1),
                                   dst.PlaneData(1), dst.PlaneLinesize(1),
                                   layout.width, layout.height, 2, 0, params);
        pixel_ops::TryPermutePlane(src.PlaneData(1), src.PlaneLinesize(1),
                                   dst.PlaneData(1), dst.PlaneLinesize(1),
                                   layout.width, layout.height, 2, 1, params);
    } else {
        pixel_ops::TryPermutePlane(src.PlaneData(1), src.PlaneLinesize(1),
                                   dst.PlaneData(1), dst.PlaneLinesize(1),
                                   layout.width, layout.height, 1, 0, params);
        pixel_ops::TryPermutePlane(src.PlaneData(2), src.PlaneLinesize(2),
                                   dst.PlaneData(2), dst.PlaneLinesize(2),
                                   layout.width, layout.height, 1, 0, params);
    }

    MediaBuffer out_buf(std::move(dst), input.timestamp(), input.flags());
    emit(std::move(out_buf));
    return true;
}

void TransformEffectNode::ApplyBilinear(const MediaFrame& src, MediaFrame& dst,
                                        MediaBuffer& input, OutputCallback& emit) {
    const TransformAffineParams params = SnapshotParams();
    pixel_ops::AffineMapping mapping =
        pixel_ops::ComputeAffineMapping(params, src.width(), src.height());
    pixel_ops::RemapPlane(src.PlaneData(0), src.PlaneLinesize(0),
                          dst.PlaneData(0), dst.PlaneLinesize(0),
                          src.width(), src.height(), 1, 0, 0, mapping);

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(src.format(), src.width(), src.height());
    if (layout.interleaved) {
        pixel_ops::RemapInterleavedPlane(src.PlaneData(1), src.PlaneLinesize(1),
                                         dst.PlaneData(1), dst.PlaneLinesize(1),
                                         layout.width, layout.height, 128, mapping);
    } else {
        pixel_ops::AffineMapping chroma_map =
            pixel_ops::ComputeAffineMapping(params, layout.width, layout.height);
        pixel_ops::RemapPlane(src.PlaneData(1), src.PlaneLinesize(1),
                              dst.PlaneData(1), dst.PlaneLinesize(1),
                              layout.width, layout.height, 1, 0, 128, chroma_map);
        pixel_ops::RemapPlane(src.PlaneData(2), src.PlaneLinesize(2),
                              dst.PlaneData(2), dst.PlaneLinesize(2),
                              layout.width, layout.height, 1, 0, 128, chroma_map);
    }

    MediaBuffer out_buf(std::move(dst), input.timestamp(), input.flags());
    emit(std::move(out_buf));
}

}  // namespace mvp::graph
