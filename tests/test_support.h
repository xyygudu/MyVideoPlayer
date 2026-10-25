#ifndef MVP_TESTS_TEST_SUPPORT_H_
#define MVP_TESTS_TEST_SUPPORT_H_

#include <chrono>
#include <functional>
#include <string>

#include "mvp/media_player.h"

namespace mvp::test {

/// Absolute path of a clip made by generate_test_media.cmake.
std::string MediaPath(const std::string& name);

/// A fresh output path under the build tree (any old file is removed).
std::string ScratchPath(const std::string& name);

/// The environment variable's value; empty when it is not set.
std::string GetEnv(const char* name);

/// Polls `done` every 10 ms, pumping this thread's window messages, until it
/// returns true or `timeout` passes. Returns the last result.
bool WaitUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout);

/// Pumps this thread's window messages for `duration`.
void PumpFor(std::chrono::milliseconds duration);

/// Average of R, G and B over the whole frame; -1 for an empty frame.
double MeanLuma(const CapturedFrame& frame);

/// Mean absolute RGB difference; 255 when the sizes differ.
double MeanAbsDiff(const CapturedFrame& a, const CapturedFrame& b);

/// The frame mirrored left to right.
CapturedFrame Mirrored(const CapturedFrame& frame);

/// Writes the frame as a binary PPM, to inspect a failure.
void SaveImage(const CapturedFrame& frame, const std::string& path);

/// A native window that is never shown: the renderer needs a real HWND.
class HiddenWindow {
  public:
    HiddenWindow(int width, int height);
    ~HiddenWindow();

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    void* handle() const { return hwnd_; }

  private:
    void* hwnd_{nullptr};
};

}  // namespace mvp::test

#endif  // MVP_TESTS_TEST_SUPPORT_H_
