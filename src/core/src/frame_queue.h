#ifndef MVP_FRAME_QUEUE_H_
#define MVP_FRAME_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

struct AVFrame;

namespace mvp {

class FrameQueue {
  public:
    explicit FrameQueue(int max_size = 4);
    ~FrameQueue();

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    // Push a frame (blocks if queue is full). Tags frame with the given serial.
    void Push(AVFrame* frame, int serial);

    // Pop a frame (blocks if queue is empty). Writes frame's serial to *out_serial.
    // Returns false if aborted.
    bool Pop(AVFrame* frame, int* out_serial);

    // Flush all frames, release memory, and increment serial (atomic operation).
    void FlushAndIncrementSerial();

    // Signal all waiting threads to wake up and abort.
    void Abort();

    int serial() const { return serial_.load(std::memory_order_acquire); }
    int Size() const;

  private:
    struct SerialFrame {
        AVFrame* frame;
        int serial;
    };

    std::queue<SerialFrame> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    int max_size_;
    std::atomic<int> serial_{0};
    bool abort_;
};

}  // namespace mvp

#endif  // MVP_FRAME_QUEUE_H_
