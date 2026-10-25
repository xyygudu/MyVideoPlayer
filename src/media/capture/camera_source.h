#ifndef MVP_CAPTURE_CAMERA_SOURCE_H_
#define MVP_CAPTURE_CAMERA_SOURCE_H_

#include <cstdint>
#include <memory>
#include <string>

namespace mvp::capture {

/// Captures a video camera as BGRA frames.
///
/// Threading: Open, Read and Close run on one thread (the capture thread);
/// Interrupt may be called from any thread.
class CameraSource {
  public:
    enum class ReadStatus {
        kFrame,    // `dst` holds a new frame
        kNoFrame,  // Nothing this time (stream gap, interrupted)
        kFailed,   // The camera is gone: Close() it and try Open() again later
    };

    virtual ~CameraSource() = default;

    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual double FrameRate() const = 0;
    virtual std::string Description() const = 0;
    /// Waits for the next frame and writes it as BGRA into `dst`
    /// (Width() x Height()).
    virtual ReadStatus Read(uint8_t* dst, int stride) = 0;
    /// Makes a Read() blocked in the device return soon, and every later
    /// Read() return at once: the source is being shut down.
    virtual void Interrupt() = 0;
};

std::unique_ptr<CameraSource> CreateCameraSource(const std::string& device_id);

}  // namespace mvp::capture

#endif  // MVP_CAPTURE_CAMERA_SOURCE_H_
