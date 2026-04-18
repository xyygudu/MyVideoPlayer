#include "clock.h"

namespace mvp {

Clock::Clock() : time_(0.0) {}

void Clock::Set(double time) { time_.store(time, std::memory_order_relaxed); }

double Clock::Get() const { return time_.load(std::memory_order_relaxed); }

void Clock::Reset() { time_.store(0.0, std::memory_order_relaxed); }

}  // namespace mvp
