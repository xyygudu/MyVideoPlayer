#ifndef MVP_FRAME_QUEUE_H_
#define MVP_FRAME_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

#include "ffmpeg_utils.h"

namespace mvp {

// Queue node: attaches serial and EOF flag during transit (same rationale as SerialPacket).
struct SerialFrame {
    AVFramePtr frame;  // empty (null) for EOF markers
    int serial;
    bool eof{false};
};

class FrameQueue {
  public:
    explicit FrameQueue(int max_size = 4);
    ~FrameQueue();

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    // Push a frame (blocks if queue is full). Takes ownership via move.
    void Push(SerialFrame sf);

    // Push an EOF marker into the queue.
    void PushEof(int serial);

    // Pop a frame (blocks if queue is empty). Returns nullopt if aborted.
    std::optional<SerialFrame> Pop();

    // Flush all frames and increment serial. Does NOT change abort state.
    void Flush();

    // Signal all waiting threads to wake up and abort. Does NOT clear data.
    void Abort();

    // Reset to initial state (abort=false, serial=0, data cleared).
    void Reset();

    int serial() const { return serial_.load(std::memory_order_acquire); }
    int Size() const;

  private:
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
