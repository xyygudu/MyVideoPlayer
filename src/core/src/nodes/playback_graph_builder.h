#ifndef MVP_NODES_PLAYBACK_GRAPH_BUILDER_H_
#define MVP_NODES_PLAYBACK_GRAPH_BUILDER_H_

#include <string>
#include <vector>

#include "graph/node.h"
#include "nodes/video_sink_node.h"  // for VideoFrameCallback

namespace mvp {
class VideoRenderer;
class Clock;
}  // namespace mvp

namespace mvp::graph {

class MediaGraph;
class OutputPort;

/// A single filter stage in a pipeline (e.g. {"scale", "1280:720"}).
/// Currently unused for playback; reserved for the filter-chain feature.
struct FilterSpec {
    std::string name;
    std::string args;
};

/// Shared dependencies for building a playback graph. Passed as one struct
/// to avoid a long, error-prone constructor parameter list (two Clock*'s
/// are easy to swap by position).
struct PlaybackContext {
    MediaGraph* graph{nullptr};
    mvp::VideoRenderer* renderer{nullptr};
    mvp::Clock* audio_clock{nullptr};
    mvp::Clock* video_clock{nullptr};
    void* window_handle{nullptr};
    VideoFrameCallback video_cb;
    bool has_audio{false};  // Determines video sync mode
};

/// Builds the per-stream pipelines of a playback graph.
///
/// Each pipeline is a linear chain: source -> Decoder -> [filters...] -> Sink.
/// Filters are a list (empty for plain playback), so inserting them later
/// requires no change to the builder — only a non-empty FilterSpec list.
class PlaybackGraphBuilder {
  public:
    explicit PlaybackGraphBuilder(const PlaybackContext& ctx) : ctx_(ctx) {}

    /// Build: source -> DecoderNode -> [filters] -> VideoSinkNode.
    void AddVideoPipeline(const StreamInfo& stream, OutputPort* source,
                          const std::vector<FilterSpec>& filters = {});

    /// Build: source -> DecoderNode -> [filters] -> AudioSinkNode.
    void AddAudioPipeline(const StreamInfo& stream, OutputPort* source,
                          const std::vector<FilterSpec>& filters = {});

  private:
    /// Connect a linear chain: source -> chain[0] -> chain[1] -> ...
    /// The first edge (source -> chain[0]) is a packet link (large capacity);
    /// the remaining edges are frame links (small capacity).
    void ConnectChain(OutputPort* source, const std::vector<INode*>& chain,
                      int first_capacity, int rest_capacity);

    PlaybackContext ctx_;
};

}  // namespace mvp::graph

#endif  // MVP_NODES_PLAYBACK_GRAPH_BUILDER_H_
