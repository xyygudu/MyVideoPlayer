#include "frame_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace mvp {

FrameQueue::FrameQueue(int max_size) : max_size_(max_size), abort_(false) {}

FrameQueue::~FrameQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
}

void FrameQueue::Push(AVFrame* frame, int serial) {
    AVFramePtr copy;
    av_frame_move_ref(copy.get(), frame);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        return;  // AVFramePtr destructor handles cleanup
    }
    queue_.push({std::move(copy), serial, false});
    cond_pop_.notify_one();
}

void FrameQueue::PushEof(int serial) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) return;
    queue_.push({AVFramePtr(), serial, true});
    cond_pop_.notify_one();
}

bool FrameQueue::Pop(AVFrame* frame, int* out_serial, bool* out_eof) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    SerialFrame sf = std::move(queue_.front());
    queue_.pop();

    if (sf.eof) {
        if (out_eof) *out_eof = true;
        *out_serial = sf.serial;
    } else {
        if (out_eof) *out_eof = false;
        av_frame_move_ref(frame, sf.frame.get());
        *out_serial = sf.serial;
    }
    cond_push_.notify_one();
    return true;
    // sf.frame destructor frees the now-empty AVFrame shell
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
