#include "packet_queue.h"

#include <spdlog/spdlog.h>

namespace mvp {

PacketQueue::PacketQueue(int64_t max_bytes)
    : max_bytes_(max_bytes), total_bytes_(0), abort_(false) {}

PacketQueue::~PacketQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
}

void PacketQueue::Push(SerialPacket sp) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || total_bytes_ < max_bytes_; });
    if (abort_) {
        return;  // AVPacketPtr destructor handles cleanup
    }
    total_bytes_ += sp.pkt->size;
    queue_.push(std::move(sp));
    cond_pop_.notify_one();
}

std::optional<SerialPacket> PacketQueue::Pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return std::nullopt;
    }
    SerialPacket sp = std::move(queue_.front());
    queue_.pop();
    total_bytes_ -= sp.pkt->size;
    cond_push_.notify_one();
    return sp;
}

void PacketQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.fetch_add(1, std::memory_order_release);
    cond_push_.notify_all();
}

void PacketQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("PacketQueue: abort (packets={}, bytes={})", queue_.size(), total_bytes_);
    abort_ = true;
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
    std::queue<SerialPacket> empty;
    queue_.swap(empty);  // AVPacketPtr destructors free all packets
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
