#ifndef MVP_DECODER_H_
#define MVP_DECODER_H_

#include <atomic>
#include <thread>

struct AVCodecContext;
struct AVFrame;
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

    AVCodecContext* CodecContext() const { return codec_ctx_; }

  private:
    void DecodeLoop();
    void EnqueueFrame(AVFrame* decoded, AVFrame* rgb_frame, int serial);

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
    int last_serial_{0};  // Tracks serial for flush-on-change
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
