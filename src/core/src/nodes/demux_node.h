#ifndef MVP_NODES_DEMUX_NODE_H_
#define MVP_NODES_DEMUX_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/node.h"
#include "graph/port.h"

struct AVFormatContext;

namespace mvp::graph {

/// Source node: opens a media file and routes compressed packets to
/// per-stream output ports.
///
/// - NodeType: kSource (no input ports)
/// - ThreadingMode: kActive (owns demux thread)
/// - Configure: requires NodeConfig::file_path
/// - Prepare: calls avformat_open_input + avformat_find_stream_info,
///            creates one OutputPort per discovered stream
/// - Start: launches DemuxLoop thread
/// - Flush: marks seek pending; worker thread executes avformat_seek_file
///
/// Lifecycle notes:
/// - format_ctx_ is allocated in Prepare(), freed in Stop()
/// - Output ports are created dynamically in Prepare() (count = nb_streams)
/// - Worker thread blocks on running_ flag + seek_requested_ atomic
class DemuxNode : public INode {
  public:
    DemuxNode();
    ~DemuxNode() override;

    // --- INode interface ---
    bool Configure(const NodeConfig& config) override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;

    void Process(MediaBuffer /*input*/, OutputCallback /*emit*/) override {
        // Source node: no-op (never called)
    }

    std::vector<InputPort*> Inputs() override { return {}; }
    std::vector<OutputPort*> Outputs() override;

    NodeType Type() const override { return NodeType::kSource; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "DemuxNode"; }

    // --- DemuxNode-specific ---

    /// Request a seek to the given position in seconds.
    /// Thread-safe. Actual seek performed in worker thread.
    void RequestSeek(double position_seconds);

    double Duration() const;
    int AudioStreamIndex() const { return audio_stream_index_; }
    int VideoStreamIndex() const { return video_stream_index_; }

  private:
    void DemuxLoop();
    void CloseFormatContext();

    NodeState state_{NodeState::kIdle};
    std::string file_path_;

    // FFmpeg state (owned, allocated in Prepare, freed in Stop)
    AVFormatContext* format_ctx_{nullptr};
    int audio_stream_index_{-1};
    int video_stream_index_{-1};

    // Output ports (one per stream, created in Prepare)
    std::vector<std::unique_ptr<OutputPort>> output_ports_;

    // Worker thread
    std::thread demux_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> seek_requested_{false};
    std::atomic<double> seek_position_{0.0};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_DEMUX_NODE_H_
