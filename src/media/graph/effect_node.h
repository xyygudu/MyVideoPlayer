#ifndef MVP_GRAPH_EFFECT_NODE_H_
#define MVP_GRAPH_EFFECT_NODE_H_

#include <string>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

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
    /// concurrently. Implementations keep their parameters behind one mutex
    /// and take a single snapshot per frame, so a frame never mixes old and
    /// new values of related parameters. Unknown ids or mismatched value
    /// types are logged via spdlog and ignored — never crash on bad input
    /// from the UI layer.
    virtual void SetParam(const std::string& id, EffectParamValue value) = 0;

    /// Restores every parameter (not the enabled flag) to its default value
    /// in one step, so no frame sees a half-reset set. Thread-safe.
    virtual void ResetParams() = 0;

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

/// SetParam helper: stores `value` into `field` when `id` names `param_id`.
/// Returns false if `id` is another parameter's; a matching id carrying the
/// wrong value type is logged and ignored (still returns true: id known).
template <typename T>
bool AssignEffectParam(const std::string& id, const char* param_id,
                       const EffectParamValue& value, T& field) {
    if (id != param_id) {
        return false;
    }
    if (const T* typed = std::get_if<T>(&value)) {
        field = *typed;
    } else {
        SPDLOG_WARN("Effect param '{}' has the wrong value type, ignored", id);
    }
    return true;
}

}  // namespace mvp::graph

#endif  // MVP_GRAPH_EFFECT_NODE_H_
