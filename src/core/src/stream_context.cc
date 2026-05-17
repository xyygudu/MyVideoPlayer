#include "stream_context.h"

#include "i_decoder.h"

namespace mvp {

StreamContext::StreamContext(std::unique_ptr<IDecoder> decoder,
                             int frame_queue_size, int64_t max_bytes)
    : packet_queue_(max_bytes),
      decoder_(std::move(decoder)),
      frame_queue_(frame_queue_size) {}

StreamContext::~StreamContext() { Stop(); }

bool StreamContext::OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx) {
    return decoder_->Open(stream, hw_ctx);
}

void StreamContext::Start() {
    auto on_frame = [this](MediaFrame frame, int serial) {
        frame_queue_.Push(
            QueueEntry<MediaFrame>{std::move(frame), serial, false});
    };
    auto on_eof = [this](int serial) { frame_queue_.PushEof(serial); };
    decoder_->Start(&packet_queue_, std::move(on_frame), std::move(on_eof));
}

void StreamContext::Stop() { decoder_->Stop(); }

void StreamContext::Flush() {
    packet_queue_.Flush();
    frame_queue_.Flush();
}

void StreamContext::Abort() {
    packet_queue_.Abort();
    frame_queue_.Abort();
    decoder_->Stop();
}

void StreamContext::Reset() {
    packet_queue_.Reset();
    frame_queue_.Reset();
}

void StreamContext::SetDropUntilPts(double pts) {
    decoder_->SetDropUntilPts(pts);
}

PacketQueue* StreamContext::GetPacketQueue() { return &packet_queue_; }

FrameQueue<MediaFrame>* StreamContext::GetFrameQueue() {
    return &frame_queue_;
}

}  // namespace mvp
