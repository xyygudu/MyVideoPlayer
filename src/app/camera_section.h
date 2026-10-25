#ifndef MVP_APP_CAMERA_SECTION_H_
#define MVP_APP_CAMERA_SECTION_H_

#include <atomic>
#include <memory>

#include <QImage>
#include <QWidget>

#include "mvp/recorder.h"

namespace mvp {
class CameraPreview;
}  // namespace mvp

class QCheckBox;
class QComboBox;
class QLabel;
class QSettings;

/// "摄像头": a picture-in-picture camera overlay for the recording, with a
/// live preview while allowed (the page is visible and not recording: a
/// camera usually serves one client at a time).
class CameraSection : public QWidget {
    Q_OBJECT

  public:
    explicit CameraSection(QWidget* parent = nullptr);
    ~CameraSection() override;

    mvp::CameraOverlayOptions Options() const;
    void SetPreviewAllowed(bool allowed);
    void RefreshDevices();

    void LoadSettings(const QSettings& settings);
    void SaveSettings(QSettings& settings) const;

  private:
    QWidget* BuildControls();
    void UpdatePreview();
    void ShowFrame(const QImage& image);

    QCheckBox* enabled_box_{nullptr};
    QComboBox* device_combo_{nullptr};
    QComboBox* corner_combo_{nullptr};
    QComboBox* size_combo_{nullptr};
    QCheckBox* mirror_box_{nullptr};
    QLabel* preview_label_{nullptr};

    std::unique_ptr<mvp::CameraPreview> preview_;
    std::string previewing_id_;     // Camera the preview runs on
    bool preview_allowed_{false};
    std::atomic<bool> frame_pending_{false};  // A frame is queued to the GUI thread
};

#endif  // MVP_APP_CAMERA_SECTION_H_
