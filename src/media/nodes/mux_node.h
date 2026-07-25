#ifndef MVP_NODES_MUX_NODE_H_
#define MVP_NODES_MUX_NODE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_graph.h"
#include "graph/node.h"
#include "graph/port.h"

struct AVFormatContext;
struct AVStream;

namespace mvp::graph {

/// Sink node: multiplexes encoded packets from one or more input streams
/// into a single output file, mirroring DemuxNode's fan-out in reverse.
///
/// - NodeType: kSink (N inputs, no output)
/// - ThreadingMode: kActive — owns a single fan-in worker thread that pulls
///   from all input Links and calls the FFmpeg muxer API, guaranteeing
///   single-threaded access (avformat_write_header/av_interleaved_write_frame
///   are not safe to call concurrently from multiple threads).
/// - Construction: `MuxNode(output_path, has_video, has_audio)` — port order
///   mirrors DemuxNode (video port first, then audio port).
/// - Prepare: avformat_alloc_output_context2 (format inferred from the
///   output path's extension), one AVStream per input port created from the
///   upstream EncoderNode's negotiated EncodedFormat, avformat_write_header.
/// - Trailer/EOS: once every input port has reported EOS, av_write_trailer
///   is called and GraphEvent::kEos is reported via the graph reference.
class MuxNode : public INode {
  public:
    explicit MuxNode(std::string output_path, bool has_video, bool has_audio);
    ~MuxNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;

    void Process(MediaBuffer /*input*/, OutputCallback /*emit*/) override {
        // Active node: no-op (uses own thread)
    }

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override { return {}; }

    NodeType Type() const override { return NodeType::kSink; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "MuxNode"; }

    /// Set graph reference for EOS/error reporting.
    void SetGraph(MediaGraph* graph) { graph_ = graph; }

    /// Invoked after each successful write of the primary stream's packet
    /// (video if present, else audio) with that packet's PTS in seconds.
    void SetProgressHook(std::function<void(double)> hook) {
        progress_hook_ = std::move(hook);
    }

  private:
    struct StreamSlot {
        std::unique_ptr<InputPort> port;
        AVStream* av_stream{nullptr};
        bool eos{false};
        bool primary{false};  // video if present, else audio
    };
    using PendingSlots = std::vector<std::optional<MediaBuffer>>;

    void MuxLoop();
    void CloseOutput();

    // Prepare helpers
    bool OpenOutput();
    bool CreateStreams();

    // MuxLoop helpers
    void FillPendingSlots(PendingSlots& pending);
    int PickNextSlot(const PendingSlots& pending) const;
    void WriteSlotPacket(StreamSlot& slot, MediaBuffer& buf);
    void DrainPending(PendingSlots& pending);
    void FinalizeOutput();
    bool AllSlotsAtEos() const;

    NodeState state_{NodeState::kIdle};
    std::string output_path_;

    AVFormatContext* format_ctx_{nullptr};
    std::vector<StreamSlot> slots_;
    bool header_written_{false};

    MediaGraph* graph_{nullptr};
    std::function<void(double)> progress_hook_;

    std::thread mux_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_MUX_NODE_H_
