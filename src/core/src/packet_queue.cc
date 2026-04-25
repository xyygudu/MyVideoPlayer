#include "packet_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace mvp {

PacketQueue::PacketQueue(int64_t max_bytes)
    : max_bytes_(max_bytes), total_bytes_(0), abort_(false) {}

PacketQueue::~PacketQueue() { Flush(); }

void PacketQueue::Push(AVPacket* pkt) {
    AVPacket* copy = av_packet_alloc();
    av_packet_move_ref(copy, pkt);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || total_bytes_ < max_bytes_; });
    if (abort_) {
        av_packet_free(&copy);
        return;
    }
    total_bytes_ += copy->size;
    queue_.push(copy);
    cond_pop_.notify_one();
}

bool PacketQueue::Pop(AVPacket* pkt) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    AVPacket* front = queue_.front();
    queue_.pop();
    total_bytes_ -= front->size;
    av_packet_move_ref(pkt, front);
    av_packet_free(&front);
    cond_push_.notify_one();
    return true;
}

void PacketQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        AVPacket* pkt = queue_.front();
        queue_.pop();
        av_packet_free(&pkt);
    }
    total_bytes_ = 0;
    abort_ = false;
    cond_push_.notify_all();
}

void PacketQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("PacketQueue: abort (packets={}, bytes={})", queue_.size(), total_bytes_);
    abort_ = true;
    cond_push_.notify_all();
    cond_pop_.notify_all();
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
