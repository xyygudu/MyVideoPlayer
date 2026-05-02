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
    AVFrame* copy = av_frame_alloc();
    av_frame_move_ref(copy, frame);

    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) {
        av_frame_free(&copy);
        return;
    }
    queue_.push({copy, serial, false});
    cond_pop_.notify_one();
}

void FrameQueue::PushEof(int serial) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_push_.wait(lock, [this] { return abort_ || static_cast<int>(queue_.size()) < max_size_; });
    if (abort_) return;
    queue_.push({nullptr, serial, true});
    cond_pop_.notify_one();
}

bool FrameQueue::Pop(AVFrame* frame, int* out_serial, bool* out_eof) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_pop_.wait(lock, [this] { return abort_ || !queue_.empty(); });
    if (abort_ && queue_.empty()) {
        return false;
    }
    SerialFrame sf = queue_.front();
    queue_.pop();

    if (sf.eof) {
        // EOF marker: no frame data to move
        if (out_eof) *out_eof = true;
        *out_serial = sf.serial;
    } else {
        if (out_eof) *out_eof = false;
        av_frame_move_ref(frame, sf.frame);
        av_frame_free(&sf.frame);
        *out_serial = sf.serial;
    }
    cond_push_.notify_one();
    return true;
}

void FrameQueue::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearLocked();
    serial_.fetch_add(1, std::memory_order_release);
    // abort_ is intentionally NOT modified — Flush is a data operation.
    cond_push_.notify_all();
}

void FrameQueue::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    SPDLOG_INFO("FrameQueue: abort (size={})", queue_.size());
    abort_ = true;
    // Data is intentionally NOT cleared — Abort is a lifecycle signal.
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
    while (!queue_.empty()) {
        SerialFrame sf = queue_.front();
        queue_.pop();
        if (sf.frame) {
            av_frame_free(&sf.frame);
        }
    }
}

int FrameQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

}  // namespace mvp
