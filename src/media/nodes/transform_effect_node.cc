#include "nodes/transform_effect_node.h"

#include <algorithm>
#include <iterator>

extern "C" {
#include <libavutil/frame.h>
}
#include <spdlog/spdlog.h>

#include "ffmpeg_utils.h"
#include "gpu/gpu_device.h"
#include "graph/media_graph.h"
#include "media_frame.h"
#include "pixel_ops.h"

namespace mvp::graph {

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

TransformEffectNode::TransformEffectNode(bool allow_gpu) : allow_gpu_(allow_gpu) {
    input_port_ = std::make_unique<InputPort>(this);
    output_port_ = std::make_unique<OutputPort>(this);
}

TransformEffectNode::~TransformEffectNode() = default;

void TransformEffectNode::DeclareCaps() {
    // Format-transparent node: relay the downstream constraint upstream so
    // the producer negotiates a format the whole remaining chain accepts.
    if (output_port_->IsConnected()) {
        input_port_->SetCaps(output_port_->Peer()->Caps());
    }
}

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
    gpu::GpuDevice* device = graph_ ? graph_->GpuDevice() : nullptr;
    gpu_pass_ = (device && allow_gpu_) ? device->CreateVideoPass(gpu::VideoKernel::kAffineRemap)
                                       : nullptr;
    if (device) {
        SPDLOG_INFO("TransformEffectNode: GPU pass {}",
                    gpu_pass_ ? "ready" : "off, hardware frames use the CPU");
    }
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
    const Settings s = LoadSettings();
    const Settings d;  // Defaults
    return {
        {"rotate_deg", "旋转角度", EffectParamType::kFloat, s.rotate_deg, d.rotate_deg, 0.0f, 360.0f, {}},
        {"flip_h", "水平翻转", EffectParamType::kBool, s.flip_h, d.flip_h, {}, {}, {}},
        {"flip_v", "垂直翻转", EffectParamType::kBool, s.flip_v, d.flip_v, {}, {}, {}},
        {"scale_x", "水平缩放", EffectParamType::kFloat, s.scale_x, d.scale_x, 0.1f, 5.0f, {}},
        {"scale_y", "垂直缩放", EffectParamType::kFloat, s.scale_y, d.scale_y, 0.1f, 5.0f, {}},
        {"translate_x", "水平平移", EffectParamType::kFloat, s.translate_x, d.translate_x, -1.0f, 1.0f, {}},
        {"translate_y", "垂直平移", EffectParamType::kFloat, s.translate_y, d.translate_y, -1.0f, 1.0f, {}},
    };
}

void TransformEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    std::lock_guard lock(params_mutex_);
    Settings& s = settings_;
    const bool known = AssignEffectParam(id, "rotate_deg", value, s.rotate_deg) ||
                       AssignEffectParam(id, "flip_h", value, s.flip_h) ||
                       AssignEffectParam(id, "flip_v", value, s.flip_v) ||
                       AssignEffectParam(id, "scale_x", value, s.scale_x) ||
                       AssignEffectParam(id, "scale_y", value, s.scale_y) ||
                       AssignEffectParam(id, "translate_x", value, s.translate_x) ||
                       AssignEffectParam(id, "translate_y", value, s.translate_y);
    if (!known) {
        SPDLOG_WARN("TransformEffectNode: unknown param '{}'", id);
    }
}

bool TransformEffectNode::IsEnabled() const {
    std::lock_guard lock(params_mutex_);
    return settings_.enabled;
}

void TransformEffectNode::ResetParams() {
    std::lock_guard lock(params_mutex_);
    Settings defaults;
    defaults.enabled = settings_.enabled;
    settings_ = defaults;
}

void TransformEffectNode::SetEnabled(bool enabled) {
    std::lock_guard lock(params_mutex_);
    settings_.enabled = enabled;
}

TransformEffectNode::Settings TransformEffectNode::LoadSettings() const {
    std::lock_guard lock(params_mutex_);
    return settings_;
}

TransformAffineParams TransformEffectNode::ToAffine(const Settings& settings) {
    TransformAffineParams p;
    p.rotate_rad = settings.rotate_deg * kPi / 180.0f;
    p.flip_h = settings.flip_h;
    p.flip_v = settings.flip_v;
    p.scale_x = settings.scale_x;
    p.scale_y = settings.scale_y;
    p.translate_x = settings.translate_x;
    p.translate_y = settings.translate_y;
    return p;
}

void TransformEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    // One snapshot per frame: every plane and path below sees the same values.
    const Settings settings = LoadSettings();
    if (!settings.enabled || !input.IsFrame()) { emit(std::move(input)); return; }

    const TransformAffineParams params = ToAffine(settings);
    if (params.rotate_rad == 0.0f && !params.flip_h && !params.flip_v &&
        params.scale_x == 1.0f && params.scale_y == 1.0f &&
        params.translate_x == 0.0f && params.translate_y == 0.0f) {
        emit(std::move(input));
        return;
    }

    if (input.AsFrame().IsHardware() && TryProcessOnGpu(input, emit, params)) {
        return;
    }
    // CPU path: hardware frames without a usable GPU pass are downloaded here.
    // FFmpeg takes the shared device lock internally for the download.
    MediaFrame downloaded;
    MediaFrame* src = &input.AsFrame();
    if (src->IsHardware()) {
        downloaded = TransferToSoftware(*src);
        if (!downloaded.IsValid()) {
            SPDLOG_WARN("TransformEffectNode: hw frame download failed");
            emit(std::move(input));
            return;
        }
        src = &downloaded;
    }

    if (!src->IsValid()) { emit(std::move(input)); return; }

    if (!IsPlanarYuvPixelFormat(src->format())) {
        if (!logged_unsupported_format_) {
            SPDLOG_WARN("TransformEffectNode: unsupported pixel format {}, passing through",
                        src->format());
            logged_unsupported_format_ = true;
        }
        emit(std::move(input));
        return;
    }

    MediaFrame out_mf = output_pool_.Acquire(src->width(), src->height(),
                                             src->format());
    if (!out_mf.IsValid()) { emit(std::move(input)); return; }
    av_frame_copy_props(out_mf.RawFrame(), src->RawFrame());  // Keep colorimetry.
    if (TryApplyPermute(*src, out_mf, params, input, emit)) return;
    ApplyBilinear(*src, out_mf, params, input, emit);
}

bool TransformEffectNode::TryProcessOnGpu(MediaBuffer& input, OutputCallback& emit,
                                          const TransformAffineParams& params) {
    if (!gpu_pass_) {
        return false;
    }
    // The inverse matrix is plane-size independent; the shader applies each
    // plane's own centre and translation, exactly like the CPU remap.
    const pixel_ops::AffineMapping mapping = pixel_ops::ComputeAffineMapping(params, 1, 1);
    gpu::AffineRemapUniforms uniforms;
    std::copy(std::begin(mapping.inv), std::end(mapping.inv), uniforms.inverse);
    uniforms.translate[0] = params.translate_x;
    uniforms.translate[1] = params.translate_y;
    MediaFrame out = gpu_pass_->Run(input.AsFrame(), &uniforms, sizeof(uniforms));
    if (!out.IsValid()) {
        if (!logged_gpu_failure_) {
            SPDLOG_WARN("TransformEffectNode: GPU pass failed, falling back to CPU");
            logged_gpu_failure_ = true;
        }
        return false;
    }
    emit(MediaBuffer(std::move(out), input.timestamp(), input.flags()));
    return true;
}

bool TransformEffectNode::TryApplyPermute(const MediaFrame& src, MediaFrame& dst,
                                          const TransformAffineParams& params,
                                          MediaBuffer& input, OutputCallback& emit) {
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
                                        const TransformAffineParams& params,
                                        MediaBuffer& input, OutputCallback& emit) {
    pixel_ops::AffineMapping mapping =
        pixel_ops::ComputeAffineMapping(params, src.width(), src.height());
    pixel_ops::RemapPlane(src.PlaneData(0), src.PlaneLinesize(0),
                          dst.PlaneData(0), dst.PlaneLinesize(0),
                          src.width(), src.height(), 1, 0, 0, mapping);

    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(src.format(), src.width(), src.height());
    // Chroma planes have their own size, hence their own centre and offsets.
    const pixel_ops::AffineMapping chroma_map =
        pixel_ops::ComputeAffineMapping(params, layout.width, layout.height);
    if (layout.interleaved) {
        pixel_ops::RemapInterleavedPlane(src.PlaneData(1), src.PlaneLinesize(1),
                                         dst.PlaneData(1), dst.PlaneLinesize(1),
                                         layout.width, layout.height, 128, chroma_map);
    } else {
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
