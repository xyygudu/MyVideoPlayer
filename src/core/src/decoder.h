#ifndef MVP_DECODER_H_
#define MVP_DECODER_H_

#include <atomic>
#include <functional>
#include <thread>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVFrame;
struct AVStream;

namespace mvp {

class PacketQueue;

// Value-type parameters extracted from AVStream at init time.
// Decoder uses these instead of holding AVStream* at runtime.
struct DecoderParams {
    AVRational time_base{0, 1};
    AVRational frame_rate{0, 1};
};

/// Decoder 解码后通过此回调输出原始帧数据。
/// 设计意图：Decoder 只负责"解码"这一单一职责，不感知下游帧类型（VideoFrame/AudioFrame）。
/// 帧的封装（av_frame_ref + 格式映射 + 入队）由 StreamContext::Start() 提供的 lambda 完成。
/// 参数：raw AVFrame*（仅在回调期间有效），pts（秒），serial（用于丢弃过期帧）。
using FrameOutputCallback = std::function<void(AVFrame* frame, double pts, int serial)>;

// Callback invoked by Decoder when EOF is reached.
using EofOutputCallback = std::function<void(int serial)>;

class Decoder {
  public:
    Decoder();
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // Initialize decoder from a stream. Extracts DecoderParams internally.
    bool Open(AVStream* stream);
    void Close();

    // Start the decode thread. Reads from packet_queue, outputs via callbacks.
    void Start(PacketQueue* packet_queue, FrameOutputCallback on_frame,
               EofOutputCallback on_eof);
    void Stop();

    // Seek 优化：设置目标 pts，解码线程会跳过 pts < target 的帧，
    // 并启用 skip_frame 跳过非参考帧解码。到达目标后自动清除。
    void SetDropUntilPts(double pts);

    AVCodecContext* CodecContext() const { return codec_ctx_; }
    const DecoderParams& Params() const { return params_; }

  private:
    void DecodeLoop();
    void DrainFrames(int serial);

    AVCodecContext* codec_ctx_;
    DecoderParams params_;

    PacketQueue* packet_queue_;
    FrameOutputCallback on_frame_;
    EofOutputCallback on_eof_;

    std::thread decode_thread_;
    std::atomic<bool> running_;
    std::atomic<double> drop_until_pts_{0};
    int last_serial_{0};
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
