#include "frame_queue.h"

#include <spdlog/spdlog.h>

#include "media_frame.h"
#include "mvp/audio_frame.h"
#include "mvp/video_frame.h"

namespace mvp {

template<typename T>
FrameQueue<T>::FrameQueue(int max_size) : max_size_(max_size), abort_(false) {}

template<typename T>
FrameQueue<T>::~FrameQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
}

template<typename T>
void FrameQueue<T>::Push(QueueEntry<T> entry) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        return;
    }
    queue_.push(std::move(entry));
    cond_pop_.notify_one();
}

template<typename T>
void FrameQueue<T>::PushEof(int serial) {
    Push(QueueEntry<T>{T{}, serial, true});
}

template<typename T>
std::optional<QueueEntry<T>> FrameQueue<T>::Pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return std::nullopt;
    }
    QueueEntry<T> entry = std::move(queue_.front());
    queue_.pop();
    cond_push_.notify_one();
    return entry;
}

template<typename T>
void FrameQueue<T>::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.fetch_add(1, std::memory_order_release);
    cond_push_.notify_all();
}

template<typename T>
void FrameQueue<T>::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("FrameQueue: abort (size={})", queue_.size());
    abort_ = true;
    cond_push_.notify_all();
    cond_pop_.notify_all();
}

template<typename T>
void FrameQueue<T>::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.store(0, std::memory_order_release);
    abort_ = false;
    cond_push_.notify_all();
}

template<typename T>
void FrameQueue<T>::ClearLocked() {
    std::queue<QueueEntry<T>> empty;
    queue_.swap(empty);
}

template<typename T>
int FrameQueue<T>::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

// Explicit instantiations
template class FrameQueue<VideoFrame>;
template class FrameQueue<AudioFrame>;
template class FrameQueue<MediaFrame>;

}  // namespace mvp
