#ifndef MVP_GRAPH_LINK_H_
#define MVP_GRAPH_LINK_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>

#include "graph/media_buffer.h"
#include "graph/media_format.h"

namespace mvp::graph {

/// Dual-dimension capacity limits for a Link.
///
/// Both `max_bytes` and `max_count` are enforced simultaneously:
/// Push blocks when EITHER limit is reached.
/// Use `INT64_MAX` / `INT_MAX` sentinels to leave a dimension unlimited.
struct LinkCapacity {
    int64_t max_bytes{std::numeric_limits<int64_t>::max()};
    int     max_count{std::numeric_limits<int>::max()};

    /// Compute the byte contribution of a buffer toward the byte limit.
    static int64_t ByteSize(const MediaBuffer& buf) {
        if (buf.IsPacket() && buf.AsPacket().get()) {
            return buf.AsPacket()->size;
        }
        // Frames: count as 1 byte each (byte limit is typically disabled
        // via INT64_MAX for frame links, so this value is irrelevant).
        return 1;
    }
};

/// Thread-safe bounded queue connecting two Active nodes.
///
/// Enforces dual-dimension capacity (byte count + item count):
/// Push blocks when EITHER limit is exceeded.
/// Pop blocks when empty. Both wake on Abort().
/// Serial tracks flush epochs for stale-frame detection.
class Link {
  public:
    explicit Link(LinkCapacity capacity = {})
        : capacity_(capacity),
          serial_(0),
          total_bytes_(0),
          count_(0),
          abort_(false) {}

    ~Link() { Abort(); }

    /// Push a buffer into the link. Blocks if at capacity (byte or count).
    /// Stamps the current serial onto buf.serial before enqueue.
    /// Returns false only if aborted.
    bool Push(MediaBuffer buf) {
        std::unique_lock lock(mutex_);
        cond_push_.wait(lock, [this] {
            return abort_.load(std::memory_order_relaxed) || !IsFull();
        });

        if (abort_.load(std::memory_order_relaxed)) {
            return false;
        }

        buf.set_serial(serial_.load(std::memory_order_acquire));
        total_bytes_ += LinkCapacity::ByteSize(buf);
        ++count_;
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
        total_bytes_ -= LinkCapacity::ByteSize(buf);
        --count_;
        lock.unlock();
        cond_push_.notify_one();
        return buf;
    }

    /// Clear all queued data and increment serial.
    void Flush() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        total_bytes_ = 0;
        count_ = 0;
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
        total_bytes_ = 0;
        count_ = 0;
        abort_.store(false, std::memory_order_release);
        serial_.store(0, std::memory_order_release);
    }

    int serial() const { return serial_.load(std::memory_order_acquire); }

    int Size() const {
        std::lock_guard lock(mutex_);
        return static_cast<int>(queue_.size());
    }

  private:
    /// Returns true if either dimension has reached its capacity limit.
    bool IsFull() const {
        return count_ >= capacity_.max_count ||
               total_bytes_ >= capacity_.max_bytes;
    }

    LinkCapacity capacity_;
    std::atomic<int> serial_;
    int64_t total_bytes_;
    int count_;
    std::atomic<bool> abort_;

    mutable std::mutex mutex_;
    std::condition_variable cond_push_;
    std::condition_variable cond_pop_;
    std::deque<MediaBuffer> queue_;
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_LINK_H_
