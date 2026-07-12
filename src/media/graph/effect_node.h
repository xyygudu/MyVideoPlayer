#ifndef MVP_GRAPH_EFFECT_NODE_H_
#define MVP_GRAPH_EFFECT_NODE_H_

#include <string>
#include <vector>

#include "graph/node.h"
#include "mvp/effect_types.h"

namespace mvp::graph {

/// Base interface for real-time-tunable video effect nodes.
///
/// IEffectNode extends INode with a "parameter reflection" surface: the UI
/// layer discovers what parameters an effect has (name/type/range/value)
/// and adjusts them without knowing the concrete effect type.
///
/// Concrete effects (TransformEffectNode, ColorEffectNode) implement
/// Params()/SetParam() directly rather than sharing a common base class —
/// see openspec/changes/video-effect-chain/design.md for the rationale
/// (a handful of fixed parameters per effect; a shared parameter-table
/// mechanism would be premature generalization at this scale).
class IEffectNode : public INode {
  public:
    /// Full snapshot of all parameters (including current value); the UI
    /// uses this to build its controls in a single call.
    virtual std::vector<EffectParam> Params() const = 0;

    /// Set a single parameter by id. Thread-safe: called from the UI thread
    /// while the node's own processing path (its upstream Active node's
    /// thread, for the default Passive threading mode) may be reading
    /// concurrently. Unknown ids or mismatched value types are logged via
    /// spdlog and ignored — never crash on bad input from the UI layer.
    virtual void SetParam(const std::string& id, EffectParamValue value) = 0;

    /// Whether this effect currently modifies the data flowing through it.
    /// Disabled effects pass their input through unchanged in Process().
    /// Kept independent of INode::SetPaused: SetPaused mirrors the global
    /// playback transport state cascaded by MediaGraph, whereas this is a
    /// per-effect toggle the user controls in the UI — the two must not be
    /// conflated (see design.md).
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    NodeType Type() const override { return NodeType::kTransform; }
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_EFFECT_NODE_H_
