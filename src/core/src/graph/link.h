#ifndef MVP_GRAPH_LINK_H_
#define MVP_GRAPH_LINK_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "graph/media_buffer.h"
#include "graph/media_format.h"

namespace mvp::graph {

/// Capacity policy: limit by total byte size of payloads.
struct ByteCapacity {
    int64_t max_bytes{15 * 1024 * 1024};  // Default 15 MB

    static int64_t Size(const MediaBuffer& buf) {
        if (buf.IsPacket() && buf.AsPacket().get()) {
            return buf.AsPacket()->size;
        }
        // Frames: estimate from linesize * height (approximate).
        // For simplicity, count each frame as 1 unit in byte mode
        // since exact size depends on pixel format. In practice, byte
        // capacity is used for packet links.
        return 1;
    }
};

/// Capacity policy: limit by number of items.
struct CountCapacity {
    int max_count{4};

    static int64_t Size(const MediaBuffer& /*buf*/) { return 1; }
};

/// Thread-safe bounded queue connecting two Active nodes.
///
/// Template parameter CapacityPolicy determines how capacity is measured:
/// - ByteCapacity: total byte size (for compressed packet streams)
/// - CountCapacity: number of items (for decoded frame streams)
///
/// Push blocks when full; Pop blocks when empty. Both wake on Abort().
/// Serial tracks flush epochs for stale-frame detection.
template <typename CapacityPolicy>
class Link {
  public:
    explicit Link(CapacityPolicy policy = {})
        : policy_(policy), serial_(0), current_size_(0), abort_(false) {}

    ~Link() { Abort(); }

    /// Push a buffer into the link. Blocks if at capacity.
    /// Stamps the current serial onto buf.serial before enqueue.
    /// Returns false only if aborted.
    bool Push(MediaBuffer buf) {
        std::unique_lock lock(mutex_);
        cond_push_.wait(lock, [this] {
            return abort_.load(std::memory_order_relaxed) ||
                   current_size_ < MaxCapacity();
        });

        if (abort_.load(std::memory_order_relaxed)) {
            return false;
        }

        buf.set_serial(serial_.load(std::memory_order_acquire));
        current_size_ += CapacityPolicy::Size(buf);
        queue_.push_back(std::move(buf));
        lock.unlock();
        cond_pop_.notify_one();
        return true;
    }

    /// Pop a buffer from the link. Blocks if empty.
    /// Returns nullopt if aborted.
    std::optional<MediaBuffer> Pop() {
        std::unique_lock lock(mutex_);
        cond_pop_.wait(lock, [this] {
            return abort_.load(std::memory_order_relaxed) || !queue_.empty();
        });

        if (abort_.load(std::memory_order_relaxed) && queue_.empty()) {
            return std::nullopt;
        }

        MediaBuffer buf = std::move(queue_.front());
        queue_.pop_front();
        current_size_ -= CapacityPolicy::Size(buf);
        lock.unlock();
        cond_push_.notify_one();
        return buf;
    }

    /// Clear all queued data and increment serial.
    void Flush() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        current_size_ = 0;
        serial_.fetch_add(1, std::memory_order_release);
        // Wake both sides: Push waiters can re-enqueue with new serial,
        // Pop waiters will get new data from upstream after seek.
        cond_push_.notify_all();
        cond_pop_.notify_all();
    }

    /// Signal all waiting threads to wake up and exit.
    /// Does NOT clear data.
    void Abort() {
        abort_.store(true, std::memory_order_release);
        cond_push_.notify_all();
        cond_pop_.notify_all();
    }

    /// Reset to initial state (clear data, reset abort, reset serial).
    void Reset() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        current_size_ = 0;
        abort_.store(false, std::memory_order_release);
        serial_.store(0, std::memory_order_release);
    }

    int serial() const { return serial_.load(std::memory_order_acquire); }

    int Size() const {
        std::lock_guard lock(mutex_);
        return static_cast<int>(queue_.size());
    }

  private:
    int64_t MaxCapacity() const {
        if constexpr (std::is_same_v<CapacityPolicy, CountCapacity>) {
            return policy_.max_count;
        } else {
            return policy_.max_bytes;
        }
    }

    CapacityPolicy policy_;
    std::atomic<int> serial_;
    int64_t current_size_;
    std::atomic<bool> abort_;

    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    std::deque<MediaBuffer> queue_;
};

/// Type aliases for common Link configurations.
using PacketLink = Link<ByteCapacity>;
using FrameLink = Link<CountCapacity>;

}  // namespace mvp::graph

#endif  // MVP_GRAPH_LINK_H_
