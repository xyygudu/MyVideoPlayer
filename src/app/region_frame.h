#ifndef MVP_APP_REGION_FRAME_H_
#define MVP_APP_REGION_FRAME_H_

#include <QWidget>

#include "mvp/capture_devices.h"

/// Outlines the area being recorded with a border drawn just outside it:
/// red while recording, amber while paused. Click-through, always on top
/// and excluded from capture, so it never shows up in the recording.
class RegionFrame : public QWidget {
    Q_OBJECT

  public:
    RegionFrame();

    /// Surrounds `area` (physical desktop pixels); hidden when no screen shows it.
    void ShowAround(const mvp::DesktopRect& area);
    void SetPaused(bool paused);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    bool paused_{false};
};

#endif  // MVP_APP_REGION_FRAME_H_
