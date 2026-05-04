#ifndef MVP_DECODER_H_
#define MVP_DECODER_H_

#include <atomic>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVStream;

namespace mvp {

class PacketQueue;
class FrameQueue;

class Decoder {
  public:
    Decoder();
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // Initialize decoder from a stream.
    bool Open(AVStream* stream);
    void Close();

    // Start the decode thread. Reads from packet_queue, writes to frame_queue.
    void Start(PacketQueue* packet_queue, FrameQueue* frame_queue);
    void Stop();

    // Seek 优化：设置目标 pts，解码线程会跳过 pts < target 的帧，
    // 并启用 skip_frame 跳过非参考帧解码。到达目标后自动清除。
    void SetDropUntilPts(double pts);

    AVCodecContext* CodecContext() const { return codec_ctx_; }

  private:
    void DecodeLoop();

    // 解码帧并判断是否入队（seek 期间会跳过 pts < drop_until_pts_ 的帧）
    void DrainFrames(int serial);

    AVCodecContext* codec_ctx_;
    AVStream* stream_;

    PacketQueue* packet_queue_;
    FrameQueue* frame_queue_;

    std::thread decode_thread_;
    std::atomic<bool> running_;
    std::atomic<double> drop_until_pts_{0};  // Seek fast-drop target (seconds)
    int last_serial_{0};  // Tracks serial for flush-on-change
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
