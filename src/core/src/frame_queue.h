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

    // Push an EOF marker (frame=nullptr, eof=true) into the queue.
    void PushEof(int serial);

    // Pop a frame (blocks if queue is empty). Writes frame's serial to *out_serial.
    // Returns false if aborted. Check out_eof to detect end-of-stream markers.
    bool Pop(AVFrame* frame, int* out_serial, bool* out_eof = nullptr);

    // Flush all frames and increment serial. Does NOT change abort state.
    // Use for Seek.
    void Flush();

    // Signal all waiting threads to wake up and abort. Does NOT clear data.
    // Use for Stop/Close.
    void Abort();

    // Reset to initial state (abort=false, serial=0, data cleared).
    void Reset();

    int serial() const { return serial_.load(std::memory_order_acquire); }
    int Size() const;

  private:
    struct SerialFrame {
        AVFrame* frame;  // nullptr for EOF markers
        int serial;
        bool eof{false};
    };

    void ClearLocked();  // Internal: free all frames (must hold mutex_)

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
