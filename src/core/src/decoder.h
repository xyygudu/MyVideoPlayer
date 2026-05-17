#ifndef MVP_DECODER_H_
#define MVP_DECODER_H_

#include <atomic>
#include <functional>
#include <thread>

extern "C" {
#include <libavutil/rational.h>
}

#include "i_decoder.h"

struct AVCodecContext;
struct AVFrame;
struct AVStream;

namespace mvp {

class HWAccelContext;
class PacketQueue;

// Value-type parameters extracted from AVStream at init time.
// Decoder uses these instead of holding AVStream* at runtime.
struct DecoderParams {
    AVRational time_base{0, 1};
    AVRational frame_rate{0, 1};
};

/// AVFrameDecoder: decodes audio/video streams using FFmpeg's send/receive API.
/// Implements IDecoder interface. Outputs MediaFrame via callback.
class AVFrameDecoder : public IDecoder {
  public:
    AVFrameDecoder();
    ~AVFrameDecoder() override;

    AVFrameDecoder(const AVFrameDecoder&) = delete;
    AVFrameDecoder& operator=(const AVFrameDecoder&) = delete;

    // IDecoder interface
    bool Open(AVStream* stream, HWAccelContext* hw_ctx = nullptr) override;
    void SetFrameCallback(MediaFrameCallback cb) override;
    void SetEofCallback(EofOutputCallback cb) override;
    void Start(PacketQueue* packet_queue) override;
    void Stop() override;
    void SetDropUntilPts(double pts) override;

    // Non-interface accessors (internal use)
    AVCodecContext* CodecContext() const { return codec_ctx_; }
    const DecoderParams& Params() const { return params_; }

  private:
    void Close();
    void DecodeLoop();
    void DrainFrames(int serial);

    AVCodecContext* codec_ctx_;
    DecoderParams params_;
    MediaType media_type_{MediaType::kUnknown};

    PacketQueue* packet_queue_;
    MediaFrameCallback on_frame_;
    EofOutputCallback on_eof_;

    std::thread decode_thread_;
    std::atomic<bool> running_;
    std::atomic<double> drop_until_pts_{0};
    int last_serial_{0};
};

}  // namespace mvp

#endif  // MVP_DECODER_H_
