#include "frame_queue.h"

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace mvp {

FrameQueue::FrameQueue(int max_size) : max_size_(max_size), abort_(false) {}

FrameQueue::~FrameQueue() { FlushAndIncrementSerial(); }

void FrameQueue::Push(AVFrame* frame, int serial) {
    AVFrame* copy = av_frame_alloc();
    av_frame_move_ref(copy, frame);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        av_frame_free(&copy);
        return;
    }
    queue_.push({copy, serial});
    cond_pop_.notify_one();
}

bool FrameQueue::Pop(AVFrame* frame, int* out_serial) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    SerialFrame sf = queue_.front();
    queue_.pop();
    av_frame_move_ref(frame, sf.frame);
    av_frame_free(&sf.frame);
    *out_serial = sf.serial;
    cond_push_.notify_one();
    return true;
}

void FrameQueue::FlushAndIncrementSerial() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        SerialFrame sf = queue_.front();
        queue_.pop();
        av_frame_free(&sf.frame);
    }
    serial_.fetch_add(1, std::memory_order_release);
    abort_ = false;
    cond_push_.notify_all();
}

void FrameQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("FrameQueue: abort (size={})", queue_.size());
    abort_ = true;
    cond_push_.notify_all();
    cond_pop_.notify_all();
}

int FrameQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

}  // namespace mvp
