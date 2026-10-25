#ifndef MVP_RECORDER_H_
#define MVP_RECORDER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mvp/capture_devices.h"
#include "mvp/export.h"
#include "mvp/transcode_options.h"

namespace mvp {

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

enum class RecorderState {
    kIdle,
    kRecording,
    kPaused,
    kStopping,  // Finalizing the file; the completion callback follows
};

enum class RecordResult {
    kSucceeded,  // The file is complete
    kFailed,     // A device or the output failed (see the log); a partial file may remain
};

enum class AudioSourceKind { kSystemAudio, kMicrophone };

struct RecordingStats {
    double elapsed_seconds{0.0};  // Recorded time, pauses excluded
    int64_t bytes_written{0};
    int64_t frames_captured{0};
    int64_t frames_dropped{0};       // Frame slots missed because capture fell behind
    float system_audio_level{0.0f};  // Peak since the previous Stats() call, 0..1 (after volume)
    float microphone_level{0.0f};
};

/// What a recording actually captures. An optional source (audio device,
/// camera) that cannot be opened is left out rather than failing the whole
/// recording; compare with the options to tell the user.
struct RecordingSources {
    bool system_audio{false};
    bool microphone{false};
    bool camera{false};
};

/// Screen recorder built on the graph architecture:
///   Screen ─┬─(camera overlay)─► Encoder(V) ─┐
///   System audio ─┬─► Mixer ─► Encoder(A) ───┴─► Mux ─► file
///   Microphone ───┘
///
/// Threading: call the methods from one owner thread. The completion
/// callback runs on an internal thread; it may destroy the Recorder or start
/// a new recording, but must not block waiting for the owner thread.
class MVP_CORE_EXPORT Recorder {
  public:
    Recorder();
    /// Stops a running recording without invoking the completion callback.
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    /// Opens every source and the output, then starts recording. Returns
    /// false (and reports nothing) when that fails; the log says why.
    bool Start(const RecordOptions& options);
    void Pause();
    void Resume();
    /// Ends the recording: sources stop, encoders drain and the file is
    /// finalized. Returns at once; the completion callback reports the end.
    void Stop();

    RecorderState State() const;
    RecordingStats Stats() const;
    /// Valid from a successful Start() until the next one.
    RecordingSources Sources() const;
    /// Changes a source's volume while recording.
    void SetVolume(AudioSourceKind source, float volume);

    using CompletionCallback = std::function<void(RecordResult result)>;
    void SetCompletionCallback(CompletionCallback cb);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mvp

#endif  // MVP_RECORDER_H_
