#ifndef MVP_NODES_TRANSFORM_EFFECT_NODE_H_
#define MVP_NODES_TRANSFORM_EFFECT_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "graph/effect_node.h"
#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"

struct AVFrame;

namespace mvp::graph {

/// Snapshot of TransformEffectNode's geometry parameters, read once per
/// frame (not once per pixel) so the remap loops only touch plain floats.
/// A free struct (not nested in TransformEffectNode) so the plane-remapping
/// helper functions in transform_effect_node.cc — which are not members of
/// the class — can take it as a parameter.
struct TransformAffineParams {
    float rotate_rad{0.0f};
    bool flip_h{false};
    bool flip_v{false};
    float scale_x{1.0f};
    float scale_y{1.0f};
    float translate_x{0.0f};  // normalized, fraction of plane width
    float translate_y{0.0f};  // normalized, fraction of plane height
};

/// Transform node: rotation (any angle), horizontal/vertical flip, scale
/// and translate, combined into a single per-pixel remap pass.
///
/// - NodeType: kTransform (via IEffectNode)
/// - ThreadingMode: kPassive (see openspec/changes/video-effect-chain/
///   design.md for why fusing this with the decode thread is the right
///   default, and how it can be promoted to kActive later without any
///   changes to MediaGraph/Port)
///
/// Geometry is applied via backward mapping: for every destination pixel,
/// the inverse of the (rotate -> flip -> scale -> translate) affine
/// transform gives the source coordinate, which is bilinearly sampled.
/// Output frame dimensions always equal the input's — content that maps
/// outside the source frame is filled with black (Y=0, U=V=128), so
/// Negotiate() never needs to swap width/height per angle.
///
/// Only the planar/semi-planar YUV formats listed in
/// ffmpeg_utils.h/IsPlanarYuvPixelFormat() are supported; other pixel
/// formats are passed through unmodified.
class TransformEffectNode : public IEffectNode {
  public:
    TransformEffectNode();
    ~TransformEffectNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;

    void Process(MediaBuffer input, OutputCallback emit) override;

    std::vector<InputPort*> Inputs() override { return {input_port_.get()}; }
    std::vector<OutputPort*> Outputs() override { return {output_port_.get()}; }

    ThreadingMode Threading() const override { return ThreadingMode::kPassive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "TransformEffectNode"; }

    // --- IEffectNode interface ---
    std::vector<EffectParam> Params() const override;
    void SetParam(const std::string& id, EffectParamValue value) override;
    bool IsEnabled() const override { return enabled_.load(); }
    void SetEnabled(bool enabled) override { enabled_.store(enabled); }

  private:
    TransformAffineParams SnapshotParams() const;
    bool TryApplyPermute(const MediaFrame& src, MediaFrame& dst,
                         MediaBuffer& input, OutputCallback& emit);
    void ApplyBilinear(const MediaFrame& src, MediaFrame& dst,
                       MediaBuffer& input, OutputCallback& emit);

    NodeState state_{NodeState::kIdle};

    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    std::atomic<EffectParamValue> rotate_deg_{0.0f};
    std::atomic<EffectParamValue> flip_h_{false};
    std::atomic<EffectParamValue> flip_v_{false};
    std::atomic<EffectParamValue> scale_x_{1.0f};
    std::atomic<EffectParamValue> scale_y_{1.0f};
    std::atomic<EffectParamValue> translate_x_{0.0f};
    std::atomic<EffectParamValue> translate_y_{0.0f};
    std::atomic<bool> enabled_{true};

    bool logged_unsupported_format_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_TRANSFORM_EFFECT_NODE_H_
