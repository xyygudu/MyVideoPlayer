#ifndef MVP_NODES_ENCODER_NODE_H_
#define MVP_NODES_ENCODER_NODE_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/media_buffer.h"
#include "graph/media_format.h"
#include "graph/node.h"
#include "graph/port.h"
#include "media_frame.h"
#include "mvp/transcode_options.h"

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodec;
struct AVCodecContext;
struct AVAudioFifo;
struct SwsContext;
struct SwrContext;

namespace mvp::graph {

/// Transform node: encodes decoded frames into compressed packets, mirroring
/// DecoderNode in reverse (kTransform, kActive).
/// - Negotiate: resolve encoder, publish preliminary EncodedFormat (real
///   extradata only known after open).
/// - Prepare: avcodec_open2, then republish format with real codec params.
/// - EOF: null frame -> drain -> push EOS.
class EncoderNode : public INode {
  public:
    explicit EncoderNode(mvp::EncodeParams params);
    ~EncoderNode() override;

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
    std::vector<OutputPort*> Outputs() override;

    NodeType Type() const override { return NodeType::kTransform; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return name_; }

  private:
    void EncodeLoop();
    void DrainPackets();
    void CloseCodec();

    // Prepare helpers
    bool FindEncoder();
    bool OpenCodec();
    void ConfigureVideoContext(const VideoFormat& fmt);
    void ConfigureAudioContext(const AudioFormat& fmt);
    void ApplyRateControl(struct AVDictionary** opts);
    void PublishNegotiatedOutputFormat();

    // EncodeLoop helpers
    void ProcessFrame(MediaBuffer& buf);
    void HandleEos();
    MediaFrame ConvertVideoFrame(const MediaFrame& src);
    AVFramePtr ConvertAudioFrame(const MediaFrame& src);
    void SendFrameAndDrain(AVFrame* frame);

    // Audio FIFO: feed encoders exactly frame_size samples/frame (AAC: 1024).
    bool EnsureAudioFifo();
    void ProcessAudioFrame(MediaFrame& mf, int64_t pts_ticks);
    void SendCompleteAudioFrames();
    void FlushAudioFifo();

    NodeState state_{NodeState::kIdle};
    std::string name_{"EncoderNode"};

    mvp::EncodeParams params_;
    bool global_header_{false};
    const AVCodec* codec_{nullptr};
    MediaType media_type_{MediaType::kUnknown};
    AVRational time_base_{1, 1000};

    AVCodecContext* codec_ctx_{nullptr};
    SwsContext* sws_ctx_{nullptr};  // video pixel format conversion, lazy
    SwrContext* swr_ctx_{nullptr};  // audio resample, lazy
    AVAudioFifo* audio_fifo_{nullptr};  // audio frame-size buffer, lazy
    int64_t audio_next_pts_{0};  // pts (time_base units) of first unconsumed sample
    bool audio_pts_valid_{false};
    MediaFramePool video_scratch_pool_;

    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    std::thread encode_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_ENCODER_NODE_H_
