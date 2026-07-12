#ifndef MVP_NODES_COLOR_EFFECT_NODE_H_
#define MVP_NODES_COLOR_EFFECT_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "graph/effect_node.h"
#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp::graph {

/// Transform node: adjusts brightness/contrast/saturation of decoded video
/// frames.
///
/// - NodeType: kTransform (via IEffectNode)
/// - ThreadingMode: kPassive (processed synchronously on the upstream
///   Active node's thread — see openspec/changes/video-effect-chain/design.md
///   for why this is the right default for a cheap per-pixel effect)
///
/// Only the planar/semi-planar YUV formats listed in
/// ffmpeg_utils.h/IsPlanarYuvPixelFormat() are supported; frames in any other
/// pixel format (e.g. RGB32) are passed through unmodified. The check runs
/// per-frame in Process() (not Prepare()) because the format negotiated at
/// graph build time is a placeholder — DecoderNode only learns the true
/// decoded pixel format once frames actually arrive.
class ColorEffectNode : public IEffectNode {
  public:
    ColorEffectNode();
    ~ColorEffectNode() override;

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
    std::string Name() const override { return "ColorEffectNode"; }

    // --- IEffectNode interface ---
    std::vector<EffectParam> Params() const override;
    void SetParam(const std::string& id, EffectParamValue value) override;
    bool IsEnabled() const override { return enabled_.load(); }
    void SetEnabled(bool enabled) override { enabled_.store(enabled); }

  private:
    NodeState state_{NodeState::kIdle};

    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    // Parameters. Stored as std::atomic<EffectParamValue> (not floats
    // encoding bools) so the UI-thread/processing-thread boundary always
    // deals with the actual declared type — see design.md Decision 1.
    std::atomic<EffectParamValue> brightness_{0.0f};
    std::atomic<EffectParamValue> contrast_{1.0f};
    std::atomic<EffectParamValue> saturation_{1.0f};
    std::atomic<bool> enabled_{true};

    // Logs the "unsupported pixel format" warning at most once, to avoid
    // flooding spdlog every frame for an unsupported source.
    bool logged_unsupported_format_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_COLOR_EFFECT_NODE_H_
