#ifndef MVP_RECORD_OPTIONS_H_
#define MVP_RECORD_OPTIONS_H_

#include <cstdint>
#include <string>

#include "mvp/capture_devices.h"
#include "mvp/transcode_options.h"

namespace mvp {

// Separate from recorder.h so capture backends and graph nodes need not see the Recorder facade.

enum class CaptureMode {
    kMonitor,  // A whole monitor
    kRegion,   // A rectangle of the desktop
    kWindow,   // A window's area, following it when it moves (size fixed at start)
};

enum class ScreenCaptureMethod {
    kAuto,                // Desktop duplication when possible, else GDI
    kDesktopDuplication,  // DXGI: fast, one monitor at a time
    kGdi,                 // BitBlt: slower, works everywhere
};

enum class OverlayCorner { kTopLeft, kTopRight, kBottomLeft, kBottomRight };

struct VideoCaptureOptions {
    CaptureMode mode{CaptureMode::kMonitor};
    std::string monitor_id;  // kMonitor: MonitorInfo::id, empty = primary
    DesktopRect region;      // kRegion
    uint64_t window_id{0};   // kWindow: WindowInfo::id
    int frame_rate{30};
    bool capture_cursor{true};
    bool highlight_clicks{false};
    ScreenCaptureMethod method{ScreenCaptureMethod::kAuto};
};

struct AudioCaptureOptions {
    bool enabled{false};
    std::string device_id;  // AudioDeviceInfo::id, empty = system default
    float volume{1.0f};     // Linear gain, 0..2
};

struct CameraOverlayOptions {
    bool enabled{false};
    std::string device_id;  // CameraInfo::id
    OverlayCorner corner{OverlayCorner::kBottomRight};
    double width_ratio{0.25};  // Overlay width as a fraction of the video width
    bool mirror{true};         // Like a mirror, as webcam apps show the user
};

struct RecordOptions {
    VideoCaptureOptions video;
    AudioCaptureOptions system_audio;  // Loopback of an output device
    AudioCaptureOptions microphone;
    CameraOverlayOptions camera;
    /// Only codec_name and the quality (crf) are taken; the recorder picks
    /// the speed preset, GOP and B-frames for real-time encoding.
    EncodeParams video_encoder;  // e.g. "libx264", "h264_nvenc" (see CaptureDevices::VideoEncoders)
    EncodeParams audio_encoder;  // e.g. codec_name "aac" and bitrate_bps; used when any audio is on
    std::string output_path;     // Container from the extension (.mp4, .mkv)
};

}  // namespace mvp

#endif  // MVP_RECORD_OPTIONS_H_
