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

namespace mvp {
class AudioResampler;
}  // namespace mvp

namespace mvp::graph {

class MediaGraph;

/// Transform node: encodes decoded frames into compressed packets, mirroring
/// DecoderNode in reverse (kTransform, kActive).
/// - DeclareCaps: resolve encoder, declare its codec_id on the output port
///   caps so ValidateCaps() can catch a codec/container mismatch before any
///   resource is allocated (see MuxNode's caps on the other end of the link).
/// - Negotiate: pick encoder-supported parameters (pixel/sample format,
///   sample rate, channel layout) closest to the source and publish a
///   preliminary EncodedFormat (real extradata only known after open).
/// - Prepare: avcodec_open2, then republish format with real codec params.
/// - Conversion: video is scaled/converted to the encoder's size and pixel
///   format, audio resampled to its format; both adapt to mid-stream changes.
/// - EOF: flush resampler + FIFO, null frame -> drain -> push EOS.
/// - Errors: an encoder failure is posted to the graph bus as kError and
///   stops the node, so a broken output is never reported as complete.
class EncoderNode : public INode {
  public:
    explicit EncoderNode(mvp::EncodeParams params);
    ~EncoderNode() override;

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
    std::vector<OutputPort*> Outputs() override;

    NodeType Type() const override { return NodeType::kTransform; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return name_; }

    /// Set graph reference for error reporting.
    void Attach(MediaGraph* graph) override { graph_ = graph; }

  private:
    void EncodeLoop();
    /// False when encoding must stop (fatal error posted, or graph stopping).
    bool DrainPackets();
    void CloseCodec();
    /// Logs, posts kError to the graph bus and stops the encode loop.
    void Fail(const std::string& detail);

    // Prepare helpers
    bool FindEncoder();
    bool OpenCodec();
    void ConfigureVideoContext(const VideoFormat& fmt);
    void ConfigureAudioContext(const AudioFormat& fmt);
    void ApplyRateControl(struct AVDictionary** opts);
    void PublishNegotiatedOutputFormat();

    // EncodeLoop helpers
    void ProcessFrame(MediaBuffer& buf);
    void EncodeVideoFrame(MediaFrame& mf, int64_t pts_ticks);
    void HandleEos();
    MediaFrame ConvertVideoFrame(const MediaFrame& src);
    /// False when encoding must stop (see DrainPackets).
    bool SendFrameAndDrain(AVFrame* frame);

    // Audio FIFO: feed encoders exactly frame_size samples/frame (AAC: 1024).
    bool EnsureAudioFifo();
    void ProcessAudioFrame(MediaFrame& mf, int64_t pts_ticks);
    /// Resamples `in` (null = drain the resampler) into the FIFO.
    bool ResampleIntoFifo(const AVFrame* in);
    void SendCompleteAudioFrames();
    void FlushAudioFifo();

    NodeState state_{NodeState::kIdle};
    std::string name_{"EncoderNode"};
    MediaGraph* graph_{nullptr};

    mvp::EncodeParams params_;
    bool global_header_{false};
    const AVCodec* codec_{nullptr};
    MediaType media_type_{MediaType::kUnknown};
    AVRational time_base_{1, 1000};
    int sample_rate_{0};  // Encoder sample rate chosen in Negotiate (audio)

    AVCodecContext* codec_ctx_{nullptr};
    SwsContext* sws_ctx_{nullptr};  // video scale/convert, cached per source format
    std::unique_ptr<AudioResampler> resampler_;  // audio, created with the codec
    AVFramePtr resampled_;                       // resampler output, reused
    AVAudioFifo* audio_fifo_{nullptr};  // audio frame-size buffer, lazy
    int64_t audio_next_pts_{0};  // pts (time_base units) of first unconsumed sample
    bool audio_pts_valid_{false};
    // Encode thread only: last pts handed to the video encoder, which needs
    // them strictly increasing; AV_NOPTS_VALUE before the first frame.
    int64_t last_video_pts_{AV_NOPTS_VALUE};
    int64_t dropped_video_frames_{0};
    MediaFramePool video_scratch_pool_;
    bool logged_rescale_{false};
    int current_serial_{0};  // Epoch of the frame being encoded; stamps output
    bool failed_{false};     // Encode thread only: a fatal error was posted

    std::unique_ptr<InputPort> input_port_;
    std::unique_ptr<OutputPort> output_port_;

    std::thread encode_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_ENCODER_NODE_H_
