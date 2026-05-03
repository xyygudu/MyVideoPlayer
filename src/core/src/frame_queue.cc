#include "frame_queue.h"

#include <spdlog/spdlog.h>

namespace mvp {

FrameQueue::FrameQueue(int max_size) : max_size_(max_size), abort_(false) {}

FrameQueue::~FrameQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
}

void FrameQueue::Push(SerialFrame sf) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        return;  // AVFramePtr destructor handles cleanup
    }
    queue_.push(std::move(sf));
    cond_pop_.notify_one();
}

void FrameQueue::PushEof(int serial) {
    Push(SerialFrame{AVFramePtr{}, serial, true});
}

std::optional<SerialFrame> FrameQueue::Pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return std::nullopt;
    }
    SerialFrame sf = std::move(queue_.front());
    queue_.pop();
    cond_push_.notify_one();
    return sf;
}

void FrameQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.fetch_add(1, std::memory_order_release);
    cond_push_.notify_all();
}

void FrameQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("FrameQueue: abort (size={})", queue_.size());
    abort_ = true;
    cond_push_.notify_all();
    cond_pop_.notify_all();
}

void FrameQueue::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.store(0, std::memory_order_release);
    abort_ = false;
    cond_push_.notify_all();
}

void FrameQueue::ClearLocked() {
    std::queue<SerialFrame> empty;
    queue_.swap(empty);  // AVFramePtr destructors free all frames
}

int FrameQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

}  // namespace mvp
