#ifndef MVP_APP_CAPTURE_AREA_SECTION_H_
#define MVP_APP_CAPTURE_AREA_SECTION_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <QString>
#include <QWidget>

#include "mvp/recorder.h"

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSettings;
class RegionSelector;

/// "录制区域": record a whole monitor, a region picked on screen, or a
/// window (followed when it moves). While the region is being picked the
/// main window steps aside so it does not cover the desktop.
class CaptureAreaSection : public QWidget {
    Q_OBJECT

  public:
    explicit CaptureAreaSection(QWidget* parent = nullptr);

    /// Sets the target fields of `options` (mode, monitor, region, window).
    /// False, with `problem` set, when the choice is incomplete.
    bool Fill(mvp::VideoCaptureOptions* options, QString* problem) const;
    /// The physical area that will be recorded; empty when unknown.
    mvp::DesktopRect Area() const;
    mvp::CaptureMode Mode() const;
    uint64_t WindowId() const;
    /// Re-lists monitors and windows.
    void Refresh();

    void LoadSettings(const QSettings& settings);
    void SaveSettings(QSettings& settings) const;

  private:
    QWidget* BuildRegionRow();
    QWidget* BuildWindowRow();
    void RefreshMonitors();
    void RefreshWindows();
    void PickRegion();
    void OnRegionPicked(std::optional<mvp::DesktopRect> region);
    void ShowRegion();
    void UpdateEnabled();

    QRadioButton* monitor_radio_{nullptr};
    QRadioButton* region_radio_{nullptr};
    QRadioButton* window_radio_{nullptr};
    QComboBox* monitor_combo_{nullptr};
    QPushButton* pick_region_button_{nullptr};
    QLabel* region_label_{nullptr};
    QComboBox* window_combo_{nullptr};
    QPushButton* refresh_windows_button_{nullptr};
    RegionSelector* selector_{nullptr};

    std::vector<mvp::MonitorInfo> monitors_;
    std::vector<mvp::WindowInfo> windows_;
    std::optional<mvp::DesktopRect> region_;
};

#endif  // MVP_APP_CAPTURE_AREA_SECTION_H_
