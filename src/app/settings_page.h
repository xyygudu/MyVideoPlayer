#ifndef MVP_APP_SETTINGS_PAGE_H_
#define MVP_APP_SETTINGS_PAGE_H_

#include <QWidget>

namespace mvp {
class MediaPlayer;
}

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;

/// Settings and debugging page. The pipeline switches apply to the player at
/// once (an open file is rebuilt at its current position) and persist through
/// QSettings; the diagnostics refresh only while the page is visible.
class SettingsPage : public QWidget {
    Q_OBJECT

  public:
    /// player is owned by the PlayerPage, which lives as long as this page.
    explicit SettingsPage(mvp::MediaPlayer* player, QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    QWidget* BuildPipelineGroup();
    QWidget* BuildDiagnosticsGroup();
    QWidget* BuildLoggingGroup();
    QWidget* BuildDebugGroup();
    void LoadSettings();
    void ApplyPipelineOptions();
    void ApplyLogLevel(int index);
    void RefreshDiagnostics();
    void SaveScreenshot();

    mvp::MediaPlayer* player_;
    QCheckBox* hw_decode_box_{nullptr};
    QCheckBox* gpu_effects_box_{nullptr};
    QCheckBox* zero_copy_box_{nullptr};
    QLabel* state_label_{nullptr};
    QLabel* codec_label_{nullptr};
    QLabel* decoder_label_{nullptr};
    QLabel* renderer_label_{nullptr};
    QLabel* frames_label_{nullptr};
    QLabel* position_label_{nullptr};
    QComboBox* log_level_combo_{nullptr};
    QCheckBox* console_box_{nullptr};
    QLabel* debug_status_label_{nullptr};
    QTimer* refresh_timer_{nullptr};
};

#endif  // MVP_APP_SETTINGS_PAGE_H_
