#ifndef MVP_PACKET_QUEUE_H_
#define MVP_PACKET_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>

#include "sync_constants.h"

struct AVPacket;

namespace mvp {

class PacketQueue {
  public:
    explicit PacketQueue(int64_t max_bytes = sync::kDefaultMaxQueueBytes);
    ~PacketQueue();

    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    // Push a packet (blocks if byte limit reached). Takes ownership of pkt data.
    // Caller passes the serial to tag this packet with.
    void Push(AVPacket* pkt, int serial);

    // Pop a packet (blocks if queue is empty). Caller owns the returned packet.
    // Returns false if aborted. Writes the packet's serial to *out_serial.
    bool Pop(AVPacket* pkt, int* out_serial);

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
    struct SerialPacket {
        AVPacket* pkt;
        int serial;
    };

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
