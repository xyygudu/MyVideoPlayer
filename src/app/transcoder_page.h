#ifndef MVP_APP_TRANSCODER_PAGE_H_
#define MVP_APP_TRANSCODER_PAGE_H_

#include <memory>

#include <QString>
#include <QWidget>

#include "mvp/transcoder.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QFormLayout;
class QProgressBar;
class QLabel;
class QPushButton;
class IconButton;

/// Transcoder page: file pickers + basic/advanced parameter panel wired to
/// mvp::TranscodeOptions, progress reporting, start/cancel. Scoped to only
/// the capabilities mvp::Transcoder currently implements (see the
/// transcoder-ui spec) — no trim/passthrough/two-pass/hardware-encode
/// controls.
///
/// Thread safety: Transcoder's progress/completion callbacks fire on the
/// graph's internal worker threads, not the GUI thread. This class never
/// touches a QWidget directly inside those callback lambdas — each hops to
/// the GUI thread via QMetaObject::invokeMethod(..., Qt::QueuedConnection)
/// before calling HandleProgress/HandleCompletion.
class TranscoderPage : public QWidget {
    Q_OBJECT

  public:
    explicit TranscoderPage(QWidget* parent = nullptr);
    ~TranscoderPage() override;

  private slots:
    void OnBrowseSource();
    void OnBrowseOutput();
    void OnContainerChanged(int index);
    void OnQualityPresetChanged(int index);
    void OnRateControlModeChanged(int index);
    void OnAdvancedToggled();
    void OnStartOrCancelClicked();

  private:
    void SetupUi();
    QWidget* BuildFileSection();
    QWidget* BuildBasicSection();
    QWidget* BuildAdvancedSection();
    QWidget* BuildAdvancedHeader(QWidget* parent);
    QWidget* BuildAdvancedForm(QWidget* parent);
    QWidget* BuildActionSection();
    mvp::TranscodeOptions BuildOptionsFromUi() const;
    void RefreshEncoderOptions();
    void SetRunningState(bool running);
    void HandleProgress(double percent);
    void HandleCompletion(bool ok);

    QLineEdit* source_path_edit_{nullptr};
    QLineEdit* output_path_edit_{nullptr};
    IconButton* browse_source_btn_{nullptr};
    IconButton* browse_output_btn_{nullptr};
    QComboBox* container_combo_{nullptr};
    QComboBox* video_encoder_combo_{nullptr};
    QComboBox* audio_encoder_combo_{nullptr};
    QComboBox* quality_preset_combo_{nullptr};

    QWidget* advanced_body_{nullptr};
    IconButton* advanced_chevron_{nullptr};
    QFormLayout* advanced_form_{nullptr};
    QComboBox* rate_control_combo_{nullptr};
    QSpinBox* crf_spin_{nullptr};
    QSpinBox* bitrate_spin_{nullptr};
    QSpinBox* gop_spin_{nullptr};
    QSpinBox* max_b_frames_spin_{nullptr};
    QSpinBox* audio_bitrate_spin_{nullptr};

    QPushButton* start_cancel_btn_{nullptr};
    QProgressBar* progress_bar_{nullptr};
    QLabel* status_label_{nullptr};

    QString resolved_video_preset_{"medium"};
    std::unique_ptr<mvp::Transcoder> transcoder_;
    bool running_{false};
};

#endif  // MVP_APP_TRANSCODER_PAGE_H_
