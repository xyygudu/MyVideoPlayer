#ifndef MVP_PACKET_QUEUE_H_
#define MVP_PACKET_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>

#include "ffmpeg_utils.h"
#include "sync_constants.h"

namespace mvp {

// Queue node: attaches routing metadata (serial) to a packet during transit.
// serial is a property of "where in the pipeline" not of the data itself,
// so it lives here rather than inside AVPacketPtr.
struct SerialPacket {
    AVPacketPtr pkt;
    int serial;
};

class PacketQueue {
  public:
    explicit PacketQueue(int64_t max_bytes = sync::kDefaultMaxQueueBytes);
    ~PacketQueue();

    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    // Push a packet (blocks if byte limit reached). Takes ownership via move.
    void Push(SerialPacket sp);

    // Pop a packet (blocks if queue is empty). Returns nullopt if aborted.
    std::optional<SerialPacket> Pop();

    // Flush all packets and increment serial. Does NOT change abort state.
    // Use for Seek — clears stale data while keeping the queue operational.
    void Flush();

    // Signal all waiting threads to wake up and abort. Does NOT clear data.
    // Use for Stop/Close — terminates blocking Push/Pop calls.
    void Abort();

    // Reset to initial state (abort=false, serial=0, data cleared).
    // Use after Close() to prepare for reuse with a new Open().
    void Reset();

    int serial() const { return serial_.load(std::memory_order_acquire); }
    int Size() const;
    int64_t ByteSize() const;

  private:
    void ClearLocked();  // Internal: free all packets (must hold mutex_)

    std::queue<SerialPacket> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    int64_t max_bytes_;
    int64_t total_bytes_;
    std::atomic<int> serial_{0};
    bool abort_;
};

}  // namespace mvp

#endif  // MVP_PACKET_QUEUE_H_
