#ifndef MVP_DECODER_H_
#define MVP_DECODER_H_

#include <atomic>
#include <thread>

struct AVCodecContext;
struct AVStream;
struct SwsContext;

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
    // If convert_to_rgb is true, video frames are converted via sws_scale.
    void Start(PacketQueue* packet_queue, FrameQueue* frame_queue, bool convert_to_rgb = false);
    void Stop();

    // Request the decode thread to flush codec buffers (thread-safe, non-blocking).
    // The actual flush happens inside DecodeLoop before the next send_packet.
    void RequestFlush();

    // Flush the decoder buffers directly. Only safe when decode thread is NOT running.
    void FlushBuffers();

    // Returns true after the decoder has completed the requested flush.
    bool FlushCompleted() const { return flush_completed_.load(); }

    AVCodecContext* CodecContext() const { return codec_ctx_; }

  private:
    void DecodeLoop();

    AVCodecContext* codec_ctx_;
    AVStream* stream_;

    PacketQueue* packet_queue_;
    FrameQueue* frame_queue_;

    // Video format conversion
    SwsContext* sws_ctx_;
    bool convert_to_rgb_;
    int dst_width_;
    int dst_height_;

    std::thread decode_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> flush_requested_;
    std::atomic<bool> flush_completed_{true};
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
