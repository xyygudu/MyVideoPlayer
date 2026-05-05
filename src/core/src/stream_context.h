#ifndef MVP_STREAM_CONTEXT_H_
#define MVP_STREAM_CONTEXT_H_

#include "decoder.h"
#include "frame_queue.h"
#include "packet_queue.h"
#include "sync_constants.h"

struct AVStream;

namespace mvp {

class HWAccelContext;

/// Symmetric pipeline container for one media stream (audio or video).
/// Template parameter FrameType is VideoFrame or AudioFrame.
template<typename FrameType>
struct StreamContext {
    PacketQueue packet_queue;
    Decoder decoder;
    FrameQueue<FrameType> frame_queue;

    /// Construct with explicit queue sizes.
    explicit StreamContext(int frame_queue_size,
                           int64_t max_packet_bytes = sync::kDefaultMaxQueueBytes);

    /// Open the decoder for the given stream.
    bool OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx = nullptr);

    /// Start the decoder thread.
    void Start();

    /// Stop the decoder thread (blocks until joined).
    void Stop();

    /// Flush both queues (clear data + increment serials). Used for Seek.
    void Flush();

    /// Abort both queues and stop the decoder. Used for Close/Stop.
    void Abort();
};

}  // namespace mvp

#endif  // MVP_STREAM_CONTEXT_H_
