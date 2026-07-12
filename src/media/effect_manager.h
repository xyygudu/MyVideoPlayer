#ifndef MVP_EFFECT_MANAGER_H_
#define MVP_EFFECT_MANAGER_H_

#include <string>
#include <utility>
#include <vector>

#include "graph/effect_node.h"
#include "mvp/effect_types.h"

namespace mvp {

/// Indexes the IEffectNode instances wired into a MediaPlayer's graph by a
/// stable effect_id, and routes UI-facing queries/commands to them.
///
/// EffectManager is a member of MediaPlayer::Impl (not of MediaGraph):
/// "which nodes are effects and what are they called" is playback-specific
/// vocabulary that MediaGraph — a generic topology/lifecycle engine — has
/// no business knowing about. This mirrors how OBS Studio's filter list and
/// mpv's vf chain both live on the object that owns a specific pipeline,
/// not inside the generic graph/pipeline engine itself.
///
/// EffectManager does not own the nodes it indexes (MediaGraph does, via
/// AddNode) and does not decide graph topology — it is purely a query/
/// control facade over nodes that MediaPlayer::Impl has already wired.
class EffectManager {
  public:
    /// Registers a node under `effect_id` for later lookup. `node` must
    /// outlive this EffectManager (or until Clear() is called) — call
    /// Clear() before the owning MediaGraph destroys the node.
    void Register(std::string effect_id, std::string display_name,
                  graph::IEffectNode* node);

    /// Drops all registrations. Must be called before the underlying
    /// MediaGraph (and therefore the nodes) is destroyed, to avoid
    /// leaving dangling pointers behind.
    void Clear();

    /// Snapshot of every registered effect's current state, in
    /// registration order.
    std::vector<EffectInfo> Describe() const;

    /// Routes a parameter change to the named effect. Returns false (and
    /// logs a warning) if effect_id is unknown — never crashes on bad
    /// input from the UI layer.
    bool SetParam(const std::string& effect_id, const std::string& param_id,
                  EffectParamValue value);

    /// Routes an enable/disable toggle to the named effect. Returns false
    /// (and logs a warning) if effect_id is unknown.
    bool SetEnabled(const std::string& effect_id, bool enabled);

  private:
    struct Entry {
        std::string display_name;
        graph::IEffectNode* node{nullptr};  // Non-owning.
    };

    /// Registration order is preserved (vector, not unordered_map) so a
    /// future "reorder effects" feature has something to reorder — see
    /// design.md. Linear lookup is fine at the current scale (a handful of
    /// effects).
    graph::IEffectNode* Find(const std::string& effect_id) const;

    std::vector<std::pair<std::string, Entry>> effects_;
};

}  // namespace mvp

#endif  // MVP_EFFECT_MANAGER_H_
