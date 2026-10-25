#ifndef MVP_APP_RECORDER_PAGE_H_
#define MVP_APP_RECORDER_PAGE_H_

#include <string>
#include <vector>

#include <QPointer>
#include <QString>
#include <QWidget>

#include "mvp/recorder.h"
#include "recording_controller.h"

class AudioSourceRow;
class CameraSection;
class CaptureAreaSection;
class GlobalHotkeys;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QThread;

/// Screen recorder page: what to record (area, picture, sound, camera),
/// where to save it, start/pause/stop (also through global hotkeys while
/// another application has the focus) and the recent recordings. The form
/// persists through QSettings ("recorder/...").
///
/// The recording itself runs in RecordingController; this page turns the
/// form into mvp::RecordOptions and reflects the controller's state.
class RecorderPage : public QWidget {
    Q_OBJECT

  public:
    explicit RecorderPage(QWidget* parent = nullptr);
    ~RecorderPage() override;

  signals:
    /// The user wants to watch a recording.
    void PlayRequested(const QString& path);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    QWidget* BuildContent();
    QWidget* BuildActionSection();
    QWidget* BuildVideoSection();
    QWidget* BuildAudioSection();
    QWidget* BuildOutputSection();
    QWidget* BuildRecentSection();
    void ConnectController();
    void SetupHotkeys();
    void ProbeEncoders();
    void PopulateEncoders();

    void OnStartStop();
    void StartRecording();
    bool BuildOptions(mvp::RecordOptions* options, QString* problem) const;
    QString NextOutputPath() const;
    void OnPhaseChanged(RecordingController::Phase phase);
    void OnStats(const mvp::RecordingStats& stats);
    void OnFinished(const QString& path, bool succeeded);
    void ReportMissingSources();

    void LoadSettings();
    void SaveSettings() const;
    void AddRecent(const QString& path);
    void RefreshRecent();
    QString SelectedRecent() const;

    CaptureAreaSection* area_section_{nullptr};
    QWidget* video_section_{nullptr};
    QComboBox* fps_combo_{nullptr};
    QComboBox* quality_combo_{nullptr};
    QComboBox* encoder_combo_{nullptr};
    QComboBox* method_combo_{nullptr};
    QCheckBox* cursor_box_{nullptr};
    QCheckBox* clicks_box_{nullptr};
    AudioSourceRow* system_row_{nullptr};
    AudioSourceRow* mic_row_{nullptr};
    CameraSection* camera_section_{nullptr};
    QWidget* output_section_{nullptr};
    QLineEdit* folder_edit_{nullptr};
    QComboBox* container_combo_{nullptr};
    QCheckBox* countdown_box_{nullptr};
    QCheckBox* minimize_box_{nullptr};
    QPushButton* start_button_{nullptr};
    QPushButton* pause_button_{nullptr};
    QLabel* status_label_{nullptr};
    QLabel* warning_label_{nullptr};
    QLabel* hotkey_label_{nullptr};
    QListWidget* recent_list_{nullptr};

    RecordingController* controller_{nullptr};
    GlobalHotkeys* hotkeys_{nullptr};
    QPointer<QThread> encoder_probe_;
    std::vector<std::string> probed_encoders_;  // Written by the probe thread before it finishes
    mvp::RecordOptions requested_;              // What the current recording was asked to capture
    RecordingController::Phase last_phase_{RecordingController::Phase::kIdle};
};

#endif  // MVP_APP_RECORDER_PAGE_H_
