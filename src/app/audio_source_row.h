#ifndef MVP_APP_AUDIO_SOURCE_ROW_H_
#define MVP_APP_AUDIO_SOURCE_ROW_H_

#include <QString>
#include <QWidget>

#include "mvp/recorder.h"

class LevelMeter;
class QCheckBox;
class QComboBox;
class QLabel;
class QSettings;
class QSlider;

/// One audio source of a recording (system sound or microphone): an enable
/// box, the device, its volume, and a level meter that is live while
/// recording.
class AudioSourceRow : public QWidget {
    Q_OBJECT

  public:
    /// `enabled_by_default`: checked until saved settings say otherwise.
    AudioSourceRow(mvp::AudioDeviceKind kind, const QString& title, bool enabled_by_default,
                   QWidget* parent = nullptr);

    mvp::AudioCaptureOptions Options() const;
    void SetLevel(float peak);
    void ResetLevel();
    void RefreshDevices();
    /// While recording: the source and device are fixed, the volume is live.
    void SetLocked(bool locked);

    /// Keys under `prefix` (e.g. "recorder/microphone/").
    void LoadSettings(const QSettings& settings, const QString& prefix);
    void SaveSettings(QSettings& settings, const QString& prefix) const;

  signals:
    void VolumeChanged(float volume);

  private:
    void UpdateEnabled();
    void ShowVolume(int percent);

    const mvp::AudioDeviceKind kind_;
    QCheckBox* enabled_box_{nullptr};
    QComboBox* device_combo_{nullptr};
    QSlider* volume_slider_{nullptr};
    QLabel* volume_label_{nullptr};
    LevelMeter* meter_{nullptr};
    bool locked_{false};
};

#endif  // MVP_APP_AUDIO_SOURCE_ROW_H_
