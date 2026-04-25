#ifndef MVP_FRAME_QUEUE_H_
#define MVP_FRAME_QUEUE_H_

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

    // Push a frame (blocks if queue is full). Takes ownership of frame data.
    void Push(AVFrame* frame);

    // Pop a frame (blocks if queue is empty). Caller owns the returned frame.
    // Returns false if aborted.
    bool Pop(AVFrame* frame);

    // Flush all frames and release memory.
    void Flush();

    // Signal all waiting threads to wake up and abort.
    void Abort();

    int Size() const;

  private:
    std::queue<AVFrame*> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    int max_size_;
    bool abort_;
};

}  // namespace mvp

#endif  // MVP_FRAME_QUEUE_H_
