#include "packet_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace mvp {

PacketQueue::PacketQueue(int64_t max_bytes)
    : max_bytes_(max_bytes), total_bytes_(0), abort_(false) {}

PacketQueue::~PacketQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
}

void PacketQueue::Push(AVPacket* pkt, int serial) {
    AVPacket* copy = av_packet_alloc();
    av_packet_move_ref(copy, pkt);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || total_bytes_ < max_bytes_; });
    if (abort_) {
        av_packet_free(&copy);
        return;
    }
    total_bytes_ += copy->size;
    queue_.push({copy, serial});
    cond_pop_.notify_one();
}

bool PacketQueue::Pop(AVPacket* pkt, int* out_serial) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    SerialPacket sp = queue_.front();
    queue_.pop();
    total_bytes_ -= sp.pkt->size;
    av_packet_move_ref(pkt, sp.pkt);
    av_packet_free(&sp.pkt);
    *out_serial = sp.serial;
    cond_push_.notify_one();
    return true;
}

void PacketQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.fetch_add(1, std::memory_order_release);
    // Note: abort_ is intentionally NOT modified here.
    // Flush is a data operation (for Seek), not a lifecycle operation.
    cond_push_.notify_all();
}

void PacketQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("PacketQueue: abort (packets={}, bytes={})", queue_.size(), total_bytes_);
    abort_ = true;
    // Note: data is intentionally NOT cleared here.
    // Abort is a lifecycle signal, not a data operation.
    cond_push_.notify_all();
    cond_pop_.notify_all();
}

void PacketQueue::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.store(0, std::memory_order_release);
    abort_ = false;
    cond_push_.notify_all();
}

void PacketQueue::ClearLocked() {
    while (!queue_.empty()) {
        SerialPacket sp = queue_.front();
        queue_.pop();
        av_packet_free(&sp.pkt);
    }
    total_bytes_ = 0;
}

int PacketQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

int64_t PacketQueue::ByteSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

}  // namespace mvp
