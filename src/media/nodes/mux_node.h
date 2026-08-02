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
/// On all-input EOS: av_write_trailer + GraphEvent::kEos.
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

    void MuxLoop();
    void CloseOutput();

    // Prepare helpers
    bool OpenOutput();
    bool CreateStreams();

    // Container probe (av_guess_format, no allocation).
    void ResolveOutputRequirements();
    // MuxLoop helpers
    void FillPendingSlots(PendingSlots& pending);
    int PickNextSlot(const PendingSlots& pending) const;
    void WriteSlotPacket(StreamSlot& slot, MediaBuffer& buf);
    void DrainPending(PendingSlots& pending);
    void FinalizeOutput();
    bool AllSlotsAtEos() const;

    NodeState state_{NodeState::kIdle};
    std::string output_path_;
    bool needs_global_header_{false};

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
