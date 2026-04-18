#ifndef MVP_CLOCK_H_
#define MVP_CLOCK_H_

#include <atomic>

namespace mvp {

class Clock {
  public:
    Clock();

    void Set(double time);
    double Get() const;
    void Reset();

  private:
    std::atomic<double> time_;
};

}  // namespace mvp

#endif  // MVP_CLOCK_H_
