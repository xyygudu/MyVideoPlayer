#ifndef MVP_APP_REGION_SELECTOR_H_
#define MVP_APP_REGION_SELECTOR_H_

#include <optional>
#include <vector>

#include <QObject>
#include <QPointer>

#include "mvp/capture_devices.h"

class SelectionOverlay;

/// Lets the user pick the desktop area to record, like the region tools of
/// ShareX or Snipaste: every screen is covered by its frozen, dimmed
/// screenshot; dragging selects a rectangle, a click takes the window under
/// the cursor (highlighted while hovering), Esc or a right click cancels.
/// A selection stays on one screen; the result is in physical pixels.
///
/// GUI thread only. Emits exactly one of Selected() or Cancelled() per
/// Start(), then removes its overlays.
class RegionSelector : public QObject {
    Q_OBJECT

  public:
    explicit RegionSelector(QObject* parent = nullptr);
    ~RegionSelector() override;

    void Start();

  signals:
    void Selected(const mvp::DesktopRect& rect);
    void Cancelled();

  private:
    void Finish(std::optional<mvp::DesktopRect> rect);
    void CloseOverlays();

    std::vector<QPointer<SelectionOverlay>> overlays_;
    std::vector<mvp::WindowInfo> windows_;  // Snapshot taken by Start(), topmost first
};

#endif  // MVP_APP_REGION_SELECTOR_H_
