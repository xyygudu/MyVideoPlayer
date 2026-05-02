#include "clock.h"

#include <atomic>
#include <chrono>

namespace mvp {

// ---------------------------------------------------------------------------
// SeqLock write helpers
//
// The sequence counter (seq_) protocol:
//   BeginWrite: seq_ becomes odd  → signals "write in progress"
//   EndWrite:   seq_ becomes even → signals "write complete"
//
// Readers spin-retry if they observe an odd seq_ or if seq_ changed between
// the start and end of their read. This guarantees readers always see a
// consistent snapshot of {pts_, last_updated_, speed_, paused_}.
// ---------------------------------------------------------------------------

void Clock::BeginWrite() {
    // Increment seq_ to odd (write-in-progress).
    // release fence: ensures subsequent stores are not reordered before this.
    seq_.fetch_add(1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
}

void Clock::EndWrite() {
    // Increment seq_ to even (write-complete).
    // release fence: ensures prior stores are visible before seq_ update.
    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// System time source — steady_clock is monotonic and unaffected by NTP jumps.
// ---------------------------------------------------------------------------

double Clock::Now() {
    auto tp = std::chrono::steady_clock::now();
    auto dur = tp.time_since_epoch();
    return std::chrono::duration<double>(dur).count();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Clock::Clock() {
    pts_ = 0.0;
    last_updated_ = Now();
    speed_ = 1.0;
    paused_ = false;
}

// ---------------------------------------------------------------------------
// Set — update the clock to a new PTS value, recording current wall-time.
// ---------------------------------------------------------------------------

void Clock::Set(double pts) {
    BeginWrite();
    pts_ = pts;
    last_updated_ = Now();
    EndWrite();
}

// ---------------------------------------------------------------------------
// Get — read the current extrapolated clock value (lock-free via SeqLock).
//
// Algorithm:
//   1. Read seq_ (must be even, meaning no write in progress)
//   2. Copy all state fields
//   3. Read seq_ again — if it changed, a write happened during our read,
//      so the snapshot may be inconsistent. Retry from step 1.
//   4. Extrapolate: if not paused, return pts + (now - last_updated) * speed
// ---------------------------------------------------------------------------

double Clock::Get() const {
    double pts, last_updated, speed;
    bool paused;

    uint32_t s;
    do {
        // Step 1: read sequence (acquire ensures we see the latest stores)
        s = seq_.load(std::memory_order_acquire);
        if (s & 1u) {
            // Odd = write in progress, spin until writer finishes
            continue;
        }

        // Step 2: copy state (non-atomic reads — safe because writer is idle)
        pts = pts_;
        last_updated = last_updated_;
        speed = speed_;
        paused = paused_;

        // Step 3: re-read sequence — if still the same even value, snapshot is
        // consistent. acquire fence pairs with the writer's release in EndWrite.
        std::atomic_thread_fence(std::memory_order_acquire);
    } while (seq_.load(std::memory_order_acquire) != s);

    // Step 4: extrapolate
    if (paused) {
        return pts;
    }
    double elapsed = Now() - last_updated;
    return pts + elapsed * speed;
}

// ---------------------------------------------------------------------------
// SetPaused — freeze or unfreeze the clock.
//
// On pause:  snapshot the current extrapolated value into pts_ so that
//            Get() returns a fixed value regardless of elapsed time.
// On resume: reset last_updated_ to now so that elapsed restarts from zero,
//            continuing from the frozen pts_ value.
// ---------------------------------------------------------------------------

void Clock::SetPaused(bool paused) {
    BeginWrite();
    if (paused && !paused_) {
        // Transitioning to paused: freeze current extrapolated value
        double elapsed = Now() - last_updated_;
        pts_ = pts_ + elapsed * speed_;
        // last_updated_ is now stale but irrelevant while paused
    } else if (!paused && paused_) {
        // Transitioning to resumed: reset the time origin so elapsed = 0
        last_updated_ = Now();
        // pts_ stays at the frozen value — clock continues from here
    }
    paused_ = paused;
    EndWrite();
}

// ---------------------------------------------------------------------------
// SetSpeed — change the playback rate multiplier.
//
// To avoid a discontinuity, we first snapshot the current extrapolated PTS,
// then restart from that point at the new speed.
// ---------------------------------------------------------------------------

void Clock::SetSpeed(double speed) {
    BeginWrite();
    if (!paused_) {
        // Snapshot current value before changing speed
        double elapsed = Now() - last_updated_;
        pts_ = pts_ + elapsed * speed_;
        last_updated_ = Now();
    }
    speed_ = speed;
    EndWrite();
}

// ---------------------------------------------------------------------------
// Reset — equivalent to Set(pts) but semantically indicates a full reset.
// ---------------------------------------------------------------------------

void Clock::Reset(double pts) {
    BeginWrite();
    pts_ = pts;
    last_updated_ = Now();
    speed_ = 1.0;
    paused_ = false;
    EndWrite();
}

}  // namespace mvp
