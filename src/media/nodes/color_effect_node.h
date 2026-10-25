#ifndef MVP_NODES_COLOR_EFFECT_NODE_H_
#define MVP_NODES_COLOR_EFFECT_NODE_H_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "graph/effect_node.h"
#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"

namespace mvp::gpu {
class VideoPass;
}  // namespace mvp::gpu

namespace mvp::graph {

class MediaGraph;

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
    /// `allow_gpu` false keeps hardware frames off the GPU pass: they are
    /// downloaded and processed on the CPU (debug switch).
    explicit ColorEffectNode(bool allow_gpu = true);
    ~ColorEffectNode() override;

    // --- INode interface ---
    void DeclareCaps() override;
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
    void ResetParams() override;
    bool IsEnabled() const override;
    void SetEnabled(bool enabled) override;

    void Attach(MediaGraph* graph) override { graph_ = graph; }

  private:
    /// User-facing settings, written by the UI thread and read once per
    /// frame by the processing thread as one consistent snapshot.
    struct Settings {
        float brightness{0.0f};
        float contrast{1.0f};
        float saturation{1.0f};
        bool enabled{true};
    };

    Settings LoadSettings() const;
    /// Hardware frames through the GPU pass; false = not handled (no pass or it failed).
    bool TryProcessOnGpu(MediaBuffer& input, OutputCallback& emit, float brightness,
                         float contrast, float saturation);
    /// LUT path; hardware frames are downloaded at the node boundary.
    void ProcessOnCpu(MediaBuffer input, OutputCallback& emit, float brightness,
                      float contrast, float saturation);

    NodeState state_{NodeState::kIdle};
    const bool allow_gpu_;

    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    // A few values copied once per frame: a plain mutex never yields a torn
    // mix of old and new parameters, unlike independent per-field atomics.
    mutable std::mutex params_mutex_;
    Settings settings_;  // Guarded by params_mutex_.

    // Logs the "unsupported pixel format" warning at most once, to avoid
    // flooding spdlog every frame for an unsupported source.
    bool logged_unsupported_format_{false};

    MediaGraph* graph_{nullptr};
    // Created in Prepare from the graph GPU device; null = CPU path only.
    // Used on the processing (upstream decode) thread only.
    std::unique_ptr<gpu::VideoPass> gpu_pass_;
    bool logged_gpu_failure_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_COLOR_EFFECT_NODE_H_
