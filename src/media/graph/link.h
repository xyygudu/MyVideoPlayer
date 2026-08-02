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

// Provenance: ffplay's MAX_QUEUE_SIZE bounds read-ahead by bytes; the count
// bound keeps low-bitrate streams from buffering minutes of packets.
inline constexpr int64_t kPacketQueueBytes = 15 * 1024 * 1024;
inline constexpr int kPacketQueueCount = 256;

// Frame links are depth-controlled; this only guards extreme resolutions
// (4K x3 = 37MB passes through; 8K 10-bit degrades to 1 frame, not OOM).
inline constexpr int64_t kFrameQueueByteCap = 128 * 1024 * 1024;

/// Dual-dimension capacity limits for a Link: Push blocks when EITHER the
/// byte or the item limit is reached.
///
/// Only constructible through the named factories — an unbounded capacity is
/// deliberately not expressible, since for an Active downstream it is always
/// wrong and its failure mode (silently losing backpressure) is invisible.
class LinkCapacity {
  public:
    static LinkCapacity ForPackets() {
        return LinkCapacity(kPacketQueueBytes, kPacketQueueCount);
    }

    /// @param depth  How many frames to buffer; the controlling dimension.
    static LinkCapacity ForFrames(int depth) {
        return LinkCapacity(kFrameQueueByteCap, depth);
    }

    int64_t max_bytes() const { return max_bytes_; }
    int max_count() const { return max_count_; }

    /// System memory a buffer occupies. Hardware frames land near 0 because
    /// their picture data lives in GPU memory, which is what we want here.
    static int64_t ByteSize(const MediaBuffer& buf) {
        if (buf.IsPacket()) {
            const AVPacketPtr& pkt = buf.AsPacket();
            return pkt.get() ? pkt->size : 0;
        }
        if (buf.IsFrame()) {
            const AVFrame* f = buf.AsFrame().RawFrame();
            if (!f) {
                return 0;
            }
            int64_t total = 0;
            for (int i = 0; i < AV_NUM_DATA_POINTERS && f->buf[i]; ++i) {
                total += f->buf[i]->size;
            }
            for (int i = 0; i < f->nb_extended_buf; ++i) {
                total += f->extended_buf[i]->size;
            }
            return total;
        }
        return 0;  // EOS-only buffer
    }

  private:
    LinkCapacity(int64_t max_bytes, int max_count)
        : max_bytes_(max_bytes), max_count_(max_count) {}

    int64_t max_bytes_;
    int max_count_;
};

/// Thread-safe bounded queue connecting two Active nodes.
///
/// Enforces dual-dimension capacity (byte count + item count):
/// Push blocks when EITHER limit is exceeded.
/// Pop blocks when empty. Both wake on Abort().
///
/// Holds no seek epoch: staleness is a graph-wide notion owned by MediaGraph
/// and checked at the port boundary.
class Link {
  public:
    explicit Link(LinkCapacity capacity)
        : capacity_(capacity),
          total_bytes_(0),
          count_(0),
          abort_(false) {}

    ~Link() { Abort(); }

    /// Push a buffer into the link. Blocks if at capacity (byte or count).
    /// Does not stamp buf's serial — producer must set it beforehand.
    /// Returns false only if aborted.
    bool Push(MediaBuffer buf) {
        std::unique_lock lock(mutex_);
        cond_push_.wait(lock, [this] {
            return abort_.load(std::memory_order_relaxed) || !IsFull();
        });

        if (abort_.load(std::memory_order_relaxed)) {
            return false;
        }

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

    /// Clear all queued data and wake both sides.
    void Flush() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        total_bytes_ = 0;
        count_ = 0;
        // Wake both sides: Push waiters can re-enqueue, Pop waiters will get
        // new data from upstream after seek.
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

    /// Reset to initial state (clear data, reset abort).
    void Reset() {
        std::lock_guard lock(mutex_);
        queue_.clear();
        total_bytes_ = 0;
        count_ = 0;
        abort_.store(false, std::memory_order_release);
    }

    int Size() const {
        std::lock_guard lock(mutex_);
        return static_cast<int>(queue_.size());
    }

  private:
    /// Returns true if either dimension has reached its capacity limit.
    bool IsFull() const {
        return count_ >= capacity_.max_count() ||
               total_bytes_ >= capacity_.max_bytes();
    }

    LinkCapacity capacity_;
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
