#include "frame_queue.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace mvp {

FrameQueue::FrameQueue(int max_size) : max_size_(max_size), abort_(false) {}

FrameQueue::~FrameQueue() { Flush(); }

void FrameQueue::Push(AVFrame* frame) {
    AVFrame* copy = av_frame_alloc();
    av_frame_move_ref(copy, frame);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        av_frame_free(&copy);
        return;
    }
    queue_.push(copy);
    cond_pop_.notify_one();
}

bool FrameQueue::Pop(AVFrame* frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    AVFrame* front = queue_.front();
    queue_.pop();
    av_frame_move_ref(frame, front);
    av_frame_free(&front);
    cond_push_.notify_one();
    return true;
}

void FrameQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        AVFrame* frame = queue_.front();
        queue_.pop();
        av_frame_free(&frame);
    }
    cond_push_.notify_all();
}

void FrameQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    abort_ = true;
    cond_push_.notify_all();
    cond_pop_.notify_all();
}

int FrameQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

}  // namespace mvp
