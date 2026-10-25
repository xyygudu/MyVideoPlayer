#ifndef MVP_APP_RECORDING_CONTROLLER_H_
#define MVP_APP_RECORDING_CONTROLLER_H_

#include <cstdint>
#include <memory>

#include <QObject>
#include <QString>

#include "mvp/recorder.h"

class CountdownOverlay;
class QScreen;
class QTimer;
class RecordingToolbar;
class RegionFrame;

/// Runs one recording at a time for the UI: the countdown, mvp::Recorder
/// and the on-screen helpers (a frame around the recorded area, the
/// floating toolbar). Brings the recorder's completion, which arrives on an
/// internal thread, over to the GUI thread.
///
/// GUI thread only.
class RecordingController : public QObject {
    Q_OBJECT

  public:
    enum class Phase { kIdle, kCountdown, kRecording, kPaused, kStopping };

    /// What to show around a recording.
    struct Presentation {
        int countdown_seconds{0};
        mvp::DesktopRect area;     // What is recorded (physical pixels)
        bool outline{false};       // Frame `area` while recording
        uint64_t follow_window{0}; // Window mode: the frame follows this window
    };

    explicit RecordingController(QObject* parent = nullptr);
    ~RecordingController() override;

    /// Runs the countdown, if any, then starts recording; StartFailed() if
    /// the recorder cannot start.
    void Start(const mvp::RecordOptions& options, const Presentation& presentation);
    void TogglePause();
    /// Ends the recording, or cancels the countdown.
    void Stop();

    Phase phase() const { return phase_; }
    /// The sources of the current or last recording.
    mvp::RecordingSources Sources() const;
    void SetVolume(mvp::AudioSourceKind source, float volume);

  signals:
    void PhaseChanged(RecordingController::Phase phase);
    void StatsUpdated(const mvp::RecordingStats& stats);
    /// The recording ended; `succeeded` is false when it failed (see the log).
    void Finished(const QString& path, bool succeeded);
    void StartFailed();

  private:
    void BeginRecording();
    void ShowHelpers();
    void HideHelpers();
    void OnStatsTick();
    void OnCompleted(mvp::RecordResult result);
    void SetPhase(Phase phase);
    QScreen* TargetScreen() const;

    std::unique_ptr<mvp::Recorder> recorder_;
    mvp::RecordOptions options_;
    Presentation presentation_;
    Phase phase_{Phase::kIdle};

    // Top-level windows, hence owned here rather than by a parent widget.
    std::unique_ptr<CountdownOverlay> countdown_;
    std::unique_ptr<RegionFrame> frame_;
    std::unique_ptr<RecordingToolbar> toolbar_;
    QTimer* stats_timer_{nullptr};
};

#endif  // MVP_APP_RECORDING_CONTROLLER_H_
