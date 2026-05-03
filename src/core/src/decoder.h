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

    AVCodecContext* CodecContext() const { return codec_ctx_; }

  private:
    void DecodeLoop();

    AVCodecContext* codec_ctx_;
    AVStream* stream_;

    PacketQueue* packet_queue_;
    FrameQueue* frame_queue_;

    std::thread decode_thread_;
    std::atomic<bool> running_;
    int last_serial_{0};  // Tracks serial for flush-on-change
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
