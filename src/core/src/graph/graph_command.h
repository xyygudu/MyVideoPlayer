#ifndef MVP_GRAPH_GRAPH_COMMAND_H_
#define MVP_GRAPH_GRAPH_COMMAND_H_

namespace mvp::graph {

/// High-level control intents dispatched into the graph.
///
/// A Command expresses WHAT the user wants (seek), not the mechanism steps
/// (flush/drop/reposition). Nodes decide internally how to react in
/// INode::OnCommand. New intents extend the enum without changing the
/// INode interface (open/closed principle).
enum class CommandType {
    kSeek,  // Reposition playback to a target position
};

struct Command {
    CommandType type;
    double position{0.0};  // For kSeek: target position in seconds
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_GRAPH_COMMAND_H_
