#ifndef MVP_CLOCK_H_
#define MVP_CLOCK_H_

#include <atomic>
#include <cstdint>
#include <mutex>

namespace mvp {

/// Abstract time base for A/V sync.
///
/// Declared here rather than in the graph layer so the concrete Clock can
/// implement it without pulling MediaGraph into this leaf header.
class IClock {
  public:
    virtual ~IClock() = default;
    virtual void Set(double pts) = 0;
    virtual double Get() const = 0;
    virtual void SetPaused(bool paused) = 0;
    virtual void SetSpeed(double speed) = 0;
    virtual void Reset(double pts) = 0;
};

/// A wall-time-aware clock that linearly extrapolates between Set() calls.
///
/// Thread-safety: a full SeqLock — a sequence counter for lock-free readers
/// plus a mutex that serializes writers. Writers are the owning node thread
/// (per-frame PTS updates) and the control thread (pause/reset); readers are
/// the render and UI threads.
///
/// SeqLock overview:
///   - A sequence counter (seq_) starts at 0 (even).
///   - Writer increments seq_ to odd before modifying state, then increments
///     to even after finishing. This signals "write in progress" to readers.
///   - Reader reads seq_, copies all fields, reads seq_ again. If the two
///     seq_ values differ or are odd, the read was torn — retry.
///   - Result: readers never block, writer is lightweight (two atomic
///     increments plus one uncontended mutex).
class Clock : public IClock {
  public:
    Clock();

    /// Set the clock to a specific PTS value. Records current system time
    /// so that Get() can extrapolate forward from this point.
    void Set(double pts) override;

    /// Get the current clock value. If not paused, returns:
    ///   pts + (now - last_updated) * speed
    /// If paused, returns the frozen pts value.
    /// Thread-safe (lock-free read via SeqLock retry loop).
    double Get() const override;

    /// Pause or resume the clock.
    /// - Pause: snapshots the current extrapolated value into pts_ (freezes).
    /// - Resume: resets last_updated_ to now so elapsed restarts from zero.
    void SetPaused(bool paused) override;

    /// Set playback speed multiplier (default 1.0).
    void SetSpeed(double speed) override;

    /// Reset clock to a given PTS (default 0).
    void Reset(double pts = 0.0) override;

    /// Returns the current system time in seconds (steady_clock based).
    static double Now();

  private:
    // --- SeqLock write helpers ---
    // Caller must hold write_mutex_, call BeginWrite() before modifying
    // state_, EndWrite() after.
    void BeginWrite();
    void EndWrite();

    // Serializes writers; readers never touch it.
    std::mutex write_mutex_;

    // Sequence counter for SeqLock. Even = idle, odd = write in progress.
    mutable std::atomic<uint32_t> seq_{0};

    // --- Protected state (only modified between BeginWrite/EndWrite) ---
    double pts_{0.0};           // Last set PTS value
    double last_updated_{0.0};  // System time (seconds) when pts_ was set
    double speed_{1.0};         // Playback speed multiplier
    bool paused_{false};        // Whether the clock is frozen
};

}  // namespace mvp

#endif  // MVP_CLOCK_H_
