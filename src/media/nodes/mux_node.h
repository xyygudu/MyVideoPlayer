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

/// Sink node: multiplexes encoded packets into a file (kSink, kActive).
/// Fan-in thread pulls from all input links and calls the FFmpeg muxer API
/// single-threaded. Container inferred from output path extension; the
/// negotiated global-header requirement is published onto input ports.
///
/// Outcomes, all decided and finished on the mux thread:
/// - All inputs at EOS: trailer + file close, then kEos on the graph bus
///   (kError instead if finalizing failed), so "complete" means on disk.
/// - Output IO error: stop writing, close the file, post kError.
/// - Links aborted (cancel/stop): write what is pending plus a best-effort
///   trailer so the partial file stays playable; nothing is posted.
class MuxNode : public INode {
  public:
    explicit MuxNode(std::string output_path, bool has_video, bool has_audio);
    ~MuxNode() override;

    // --- INode interface ---
    void DeclareCaps() override;
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
    void Attach(MediaGraph* graph) override { graph_ = graph; }

    /// Invoked after each successful write of the primary stream's packet
    /// (video if present, else audio) with that packet's PTS in seconds.
    void SetProgressHook(std::function<void(double)> hook) {
        progress_hook_ = std::move(hook);
    }

  private:
    struct StreamSlot {
        std::unique_ptr<InputPort> port;
        AVStream* av_stream{nullptr};
        MediaType media_type{};
        bool eos{false};
        bool primary{false};  // video if present, else audio
    };
    using PendingSlots = std::vector<std::optional<MediaBuffer>>;
    enum class MuxOutcome { kEos, kAborted, kFailed };

    void MuxLoop();
    void CloseOutput();
    /// Closes the file and frees the muxer; returns the avio_closep result.
    int CloseOutputFile();

    // Prepare helpers
    bool OpenOutput();
    bool CreateStreams();

    // Container probe (av_guess_format, no allocation).
    void ResolveOutputRequirements();
    // MuxLoop helpers
    MuxOutcome RunInterleave(PendingSlots& pending);
    /// False once an input link was aborted (the graph is stopping).
    bool FillPendingSlots(PendingSlots& pending);
    int PickNextSlot(const PendingSlots& pending) const;
    /// False when the output became unwritable; a packet the muxer merely
    /// rejects (e.g. bad timestamps) is logged and dropped.
    bool WriteSlotPacket(StreamSlot& slot, MediaBuffer& buf);
    void DrainPending(PendingSlots& pending);
    void FinishOutput(MuxOutcome outcome, PendingSlots& pending);
    bool AllSlotsAtEos() const;

    NodeState state_{NodeState::kIdle};
    std::string output_path_;
    bool needs_global_header_{false};

    AVFormatContext* format_ctx_{nullptr};
    std::vector<StreamSlot> slots_;
    bool header_written_{false};
    std::string write_error_;  // Mux thread only: why the output failed

    MediaGraph* graph_{nullptr};
    std::function<void(double)> progress_hook_;

    std::thread mux_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_MUX_NODE_H_
