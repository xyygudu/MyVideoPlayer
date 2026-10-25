#include "nodes/color_effect_node.h"

#include <algorithm>
#include <utility>

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

void ApplyColorLut(MediaFrame& mf, float brightness, float contrast, float saturation) {
    auto lut = pixel_ops::BuildColorLut(brightness, contrast, saturation);
    pixel_ops::ApplyLut(mf.PlaneData(0), mf.PlaneLinesize(0), mf.width(), mf.height(), lut.y);

    // U and V share one LUT, so an interleaved plane is processed as whole rows of bytes.
    ChromaPlaneLayout layout = ComputeChromaPlaneLayout(mf.format(), mf.width(), mf.height());
    int plane_count = layout.interleaved ? 1 : 2;
    for (int p = 0; p < plane_count; ++p) {
        pixel_ops::ApplyLut(mf.PlaneData(1 + p), mf.PlaneLinesize(1 + p),
                            layout.row_bytes, layout.height, lut.uv);
    }
}

}  // namespace

ColorEffectNode::ColorEffectNode(bool allow_gpu) : allow_gpu_(allow_gpu) {
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
    gpu::GpuDevice* device = graph_ ? graph_->GpuDevice() : nullptr;
    gpu_pass_ = (device && allow_gpu_) ? device->CreateVideoPass(gpu::VideoKernel::kColorAdjust)
                                       : nullptr;
    if (device) {
        SPDLOG_INFO("ColorEffectNode: GPU pass {}",
                    gpu_pass_ ? "ready" : "off, hardware frames use the CPU");
    }
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
    const Settings s = LoadSettings();
    const Settings d;  // Defaults
    return {
        {"brightness", "亮度", EffectParamType::kFloat, s.brightness, d.brightness, -1.0f, 1.0f, {}},
        {"contrast", "对比度", EffectParamType::kFloat, s.contrast, d.contrast, 0.0f, 3.0f, {}},
        {"saturation", "饱和度", EffectParamType::kFloat, s.saturation, d.saturation, 0.0f, 3.0f, {}},
    };
}

void ColorEffectNode::SetParam(const std::string& id, EffectParamValue value) {
    std::lock_guard lock(params_mutex_);
    const bool known = AssignEffectParam(id, "brightness", value, settings_.brightness) ||
                       AssignEffectParam(id, "contrast", value, settings_.contrast) ||
                       AssignEffectParam(id, "saturation", value, settings_.saturation);
    if (!known) {
        SPDLOG_WARN("ColorEffectNode: unknown param '{}'", id);
    }
}

bool ColorEffectNode::IsEnabled() const {
    std::lock_guard lock(params_mutex_);
    return settings_.enabled;
}

void ColorEffectNode::ResetParams() {
    std::lock_guard lock(params_mutex_);
    Settings defaults;
    defaults.enabled = settings_.enabled;
    settings_ = defaults;
}

void ColorEffectNode::SetEnabled(bool enabled) {
    std::lock_guard lock(params_mutex_);
    settings_.enabled = enabled;
}

ColorEffectNode::Settings ColorEffectNode::LoadSettings() const {
    std::lock_guard lock(params_mutex_);
    return settings_;
}

void ColorEffectNode::Process(MediaBuffer input, OutputCallback emit) {
    const Settings s = LoadSettings();  // One consistent snapshot per frame.
    if (!s.enabled || !input.IsFrame()) { emit(std::move(input)); return; }

    const float b = s.brightness;
    const float c = s.contrast;
    const float sat = s.saturation;
    if (b == 0.0f && c == 1.0f && sat == 1.0f) { emit(std::move(input)); return; }

    // Hardware in, hardware out: toggling the effect never changes the frame domain.
    if (input.AsFrame().IsHardware() && TryProcessOnGpu(input, emit, b, c, sat)) {
        return;
    }
    ProcessOnCpu(std::move(input), emit, b, c, sat);
}

bool ColorEffectNode::TryProcessOnGpu(MediaBuffer& input, OutputCallback& emit,
                                      float brightness, float contrast, float saturation) {
    if (!gpu_pass_) {
        return false;
    }
    const gpu::ColorAdjustUniforms uniforms{brightness, contrast, saturation, 0.0f};
    MediaFrame out = gpu_pass_->Run(input.AsFrame(), &uniforms, sizeof(uniforms));
    if (!out.IsValid()) {
        if (!logged_gpu_failure_) {
            SPDLOG_WARN("ColorEffectNode: GPU pass failed, falling back to CPU");
            logged_gpu_failure_ = true;
        }
        return false;
    }
    emit(MediaBuffer(std::move(out), input.timestamp(), input.flags()));
    return true;
}

void ColorEffectNode::ProcessOnCpu(MediaBuffer input, OutputCallback& emit, float b, float c,
                                   float s) {
    // Hardware frames reach here only without a usable GPU pass; they are
    // downloaded at the node boundary.
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

    ApplyColorLut(mf, b, c, s);
    emit(MediaBuffer(std::move(mf), input.timestamp(), input.flags()));
}

}  // namespace mvp::graph
