#ifndef MVP_CAPTURE_SCREEN_SOURCE_H_
#define MVP_CAPTURE_SCREEN_SOURCE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "mvp/record_options.h"

namespace mvp::capture {

/// Produces images of a desktop area (monitor, region or window), with the
/// mouse cursor when asked. Implementations hide the platform capture API.
///
/// Threading: Open, Grab and Close run on one thread (the capture thread);
/// implementations may keep thread-affine state between them.
class ScreenSource {
  public:
    virtual ~ScreenSource() = default;

    /// Resolves the target and acquires the capture API. On success Width()
    /// and Height() give the (even) frame size.
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    /// Capture method and target, for logs.
    virtual std::string Description() const = 0;
    /// Writes the current image as BGRA (alpha undefined) into `dst`. Returns
    /// false only on a failure the source cannot recover from.
    virtual bool Grab(uint8_t* dst, int stride) = 0;
};

/// The platform's implementation for `options`.
std::unique_ptr<ScreenSource> CreateScreenSource(const VideoCaptureOptions& options);

}  // namespace mvp::capture

#endif  // MVP_CAPTURE_SCREEN_SOURCE_H_
