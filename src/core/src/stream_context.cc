#include "stream_context.h"

namespace mvp {

StreamContext::StreamContext(int frame_queue_size, int64_t max_packet_bytes)
    : packet_queue(max_packet_bytes), frame_queue(frame_queue_size) {}

bool StreamContext::OpenDecoder(AVStream* stream) { return decoder.Open(stream); }

void StreamContext::Start() {
    decoder.Start(&packet_queue, &frame_queue);
}

void StreamContext::Stop() { decoder.Stop(); }

void StreamContext::Flush() {
    packet_queue.Flush();
    frame_queue.Flush();
}

void StreamContext::Abort() {
    packet_queue.Abort();
    frame_queue.Abort();
    decoder.Stop();
}

}  // namespace mvp
