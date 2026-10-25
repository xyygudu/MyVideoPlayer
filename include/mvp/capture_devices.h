#ifndef MVP_CAPTURE_DEVICES_H_
#define MVP_CAPTURE_DEVICES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mvp/export.h"

namespace mvp {

/// A rectangle of the virtual desktop in physical pixels; the primary
/// monitor's top-left corner is (0, 0), monitors to its left are negative.
struct DesktopRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    bool IsEmpty() const { return width <= 0 || height <= 0; }
};

struct MonitorInfo {
    std::string id;  // Stable within a session, e.g. "\\.\DISPLAY1"
    DesktopRect bounds;
    bool primary{false};
};

struct WindowInfo {
    uint64_t id{0};  // Native window handle
    std::string title;
    DesktopRect bounds;  // Visible frame, without the drop shadow
};

struct CameraInfo {
    std::string id;  // Device path; pass it to the recorder or preview
    std::string name;
};

enum class AudioDeviceKind {
    kOutput,  // Speakers/headphones: recorded through loopback ("what you hear")
    kInput,   // Microphones
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool is_default{false};
};

/// Enumerates what a recording can capture. Synchronous; any thread.
class MVP_CORE_EXPORT CaptureDevices {
  public:
    static std::vector<MonitorInfo> Monitors();
    /// Visible top-level windows with a title, topmost first.
    static std::vector<WindowInfo> Windows();
    /// The window's current bounds; nullopt once it is closed or minimized.
    static std::optional<DesktopRect> WindowBounds(uint64_t window_id);
    static std::vector<CameraInfo> Cameras();
    static std::vector<AudioDeviceInfo> AudioDevices(AudioDeviceKind kind);
    /// H.264 encoders that actually open on this machine, hardware first,
    /// e.g. {"h264_nvenc", "libx264"}. Slow (opens each one): call it once.
    static std::vector<std::string> VideoEncoders();
};

}  // namespace mvp

#endif  // MVP_CAPTURE_DEVICES_H_
