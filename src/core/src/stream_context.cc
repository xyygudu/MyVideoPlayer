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
    if (!decoder_->Open(stream, hw_ctx)) return false;

    decoder_->SetFrameCallback([this](MediaFrame frame, int serial) {
        frame_queue_.Push(
            QueueEntry<MediaFrame>{std::move(frame), serial, false});
    });
    decoder_->SetEofCallback(
        [this](int serial) { frame_queue_.PushEof(serial); });
    return true;
}

void StreamContext::Start() {
    decoder_->Start(&packet_queue_);
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
