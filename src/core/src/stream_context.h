#ifndef MVP_STREAM_CONTEXT_H_
#define MVP_STREAM_CONTEXT_H_

#include <memory>

#include "frame_queue.h"
#include "media_frame.h"
#include "packet_queue.h"
#include "sync_constants.h"

struct AVStream;

namespace mvp {

class HWAccelContext;
class IDecoder;

/// Encapsulated pipeline for one media stream (audio or video).
/// Owns: PacketQueue → IDecoder → FrameQueue<MediaFrame>.
class StreamContext {
  public:
    StreamContext(std::unique_ptr<IDecoder> decoder, int frame_queue_size,
                  int64_t max_bytes = sync::kDefaultMaxQueueBytes);
    ~StreamContext();

    StreamContext(const StreamContext&) = delete;
    StreamContext& operator=(const StreamContext&) = delete;

    /// Open the decoder for the given stream.
    bool OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx = nullptr);

    /// Start the decoder thread. Decoded frames are pushed to frame_queue.
    void Start();

    /// Stop the decoder thread (blocks until joined).
    void Stop();

    /// Flush both queues (clear data + increment serials). Used for Seek.
    void Flush();

    /// Abort both queues and stop the decoder. Used for Close/Stop.
    void Abort();

    /// Reset both queues (re-enable after abort). Used before re-start.
    void Reset();

    /// Forward seek hint to the decoder.
    void SetDropUntilPts(double pts);

    /// Access the packet queue (for demuxer push / serial checks).
    PacketQueue* GetPacketQueue();

    /// Access the frame queue (for renderer pop).
    FrameQueue<MediaFrame>* GetFrameQueue();

  private:
    PacketQueue packet_queue_;
    std::unique_ptr<IDecoder> decoder_;
    FrameQueue<MediaFrame> frame_queue_;
};

}  // namespace mvp

#endif  // MVP_STREAM_CONTEXT_H_
