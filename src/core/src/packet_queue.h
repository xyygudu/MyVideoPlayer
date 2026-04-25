#ifndef MVP_PACKET_QUEUE_H_
#define MVP_PACKET_QUEUE_H_

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>

struct AVPacket;

namespace mvp {

class PacketQueue {
  public:
    explicit PacketQueue(int64_t max_bytes = 15 * 1024 * 1024);
    ~PacketQueue();

    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    // Push a packet (blocks if byte limit reached). Takes ownership of pkt data.
    void Push(AVPacket* pkt);

    // Pop a packet (blocks if queue is empty). Caller owns the returned packet.
    // Returns false if aborted.
    bool Pop(AVPacket* pkt);

    // Flush all packets and release memory.
    void Flush();

    // Signal all waiting threads to wake up and abort.
    void Abort();

    int Size() const;
    int64_t ByteSize() const;

  private:
    std::queue<AVPacket*> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    int64_t max_bytes_;
    int64_t total_bytes_;
    bool abort_;
};

}  // namespace mvp

#endif  // MVP_PACKET_QUEUE_H_
