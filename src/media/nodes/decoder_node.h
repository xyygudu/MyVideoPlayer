#ifndef MVP_NODES_DECODER_NODE_H_
#define MVP_NODES_DECODER_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_format.h"
#include "graph/node.h"
#include "graph/port.h"

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;

namespace mvp::graph {

class MediaGraph;  // Forward declaration to avoid circular include

/// Transform node: decodes compressed packets (AVPacket) into frames
/// (MediaFrame).
///
/// - NodeType: kTransform (1 input, 1 output)
/// - ThreadingMode: kActive (owns decode thread)
/// - Negotiate: reads codec params from input port format, infers output
///              format (VideoFormat/AudioFormat) without opening codec
/// - Prepare: creates AVCodecContext from upstream codec_id;
///            queries HW device from graph if available
/// - Flush: flushes codec internal buffers (avcodec_flush_buffers)
/// - Flush: flushes codec internal buffers (avcodec_flush_buffers)
///
/// Lifecycle notes:
/// - codec_ctx_ allocated in Prepare(), freed in Stop()
/// - Worker thread pulls from input Link, sends to output port
/// - Supports SetDropUntilPts() for seek optimization (skip non-ref frames)
/// - EOF handling: null packet → drain → push EOS downstream
class DecoderNode : public INode {
  public:
    DecoderNode();
    ~DecoderNode() override;

    // --- INode interface ---
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override;
    void OnCommand(const Command& cmd) override;

    void Process(MediaBuffer /*input*/, OutputCallback /*emit*/) override {
        // Active node: no-op (uses own thread)
    }

    std::vector<InputPort*> Inputs() override;
    std::vector<OutputPort*> Outputs() override;

    NodeType Type() const override { return NodeType::kTransform; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return name_; }

    // --- DecoderNode-specific ---

    /// Skip decoded frames until PTS >= target. Thread-safe.
    /// Used for seek optimization.
    void SetDropUntilPts(double pts);

    /// Set graph reference for shared resource access (HW device).
    void Attach(MediaGraph* graph) override { graph_ = graph; }

  private:
    void DecodeLoop();
    void DrainFrames();
    void CloseCodec();

    // Prepare helpers (resource allocation)
    bool FindAndOpenCodec(const AVCodecParameters* codecpar);

    // DecodeLoop helpers (per-packet processing)
    void MaybeFlushOnSerialChange(int serial);
    void ProcessPacket(MediaBuffer& buf);
    void HandleEos();

    NodeState state_{NodeState::kIdle};
    std::string name_{"DecoderNode"};

    // Negotiated parameters (from input port format)
    const AVCodecParameters* negotiated_codecpar_{nullptr};
    MediaGraph* graph_{nullptr};

    // FFmpeg codec state (owned, allocated in Prepare)
    AVCodecContext* codec_ctx_{nullptr};
    MediaType media_type_{MediaType::kUnknown};
    AVRational time_base_{0, 1};

    // Ports
    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    // Worker thread
    std::thread decode_thread_;
    std::atomic<bool> running_{false};
    std::atomic<double> drop_until_pts_{0.0};
    int last_serial_{0};     // Tracks seek epoch for flush-on-seek detection
    int current_serial_{0};  // Epoch of the packet being decoded; stamps output
};

}  // namespace mvp::graph

#endif  // MVP_NODES_DECODER_NODE_H_
