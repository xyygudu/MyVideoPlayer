#ifndef MVP_I_DECODER_H_
#define MVP_I_DECODER_H_

#include <functional>

#include "media_frame.h"

struct AVStream;

namespace mvp {

class HWAccelContext;
class PacketQueue;

/// Callback type: IDecoder outputs MediaFrame (with MediaType baked in).
using MediaFrameCallback = std::function<void(MediaFrame frame, int serial)>;

/// Callback invoked by IDecoder when EOF is reached.
using EofOutputCallback = std::function<void(int serial)>;

/// Abstract decoder interface.
/// Implementations: AVFrameDecoder (audio/video via send/receive API).
class IDecoder {
  public:
    virtual ~IDecoder() = default;

    virtual bool Open(AVStream* stream, HWAccelContext* hw_ctx = nullptr) = 0;
    virtual void SetFrameCallback(MediaFrameCallback cb) = 0;
    virtual void SetEofCallback(EofOutputCallback cb) = 0;
    virtual void Start(PacketQueue* packet_queue) = 0;
    virtual void Stop() = 0;
    virtual void SetDropUntilPts(double pts) = 0;
};

}  // namespace mvp

#endif  // MVP_I_DECODER_H_
