#ifndef MVP_APP_DESKTOP_MAPPING_H_
#define MVP_APP_DESKTOP_MAPPING_H_

#include <optional>
#include <vector>

#include <QRect>

#include "mvp/capture_devices.h"

class QScreen;

/// Converts between the recorder's coordinates (physical pixels of the
/// virtual desktop) and Qt's (logical pixels, each screen with its own
/// device pixel ratio on mixed-DPI desktops). Screens are matched to the
/// monitors by position (see MatchScreens).
namespace desktop_mapping {

struct ScreenMonitor {
    QScreen* screen{nullptr};
    mvp::MonitorInfo monitor;
};

/// Every screen with its physical monitor.
std::vector<ScreenMonitor> MatchScreens();

/// The screen showing the center of `physical`, if any.
std::optional<ScreenMonitor> ScreenAt(const mvp::DesktopRect& physical);

/// `physical` in Qt's global logical coordinates, mapped through the screen
/// that shows its center (invalid when no screen does).
QRect ToLogical(const mvp::DesktopRect& physical);

/// A rectangle given in `screen`'s local logical coordinates (0,0 = its
/// top-left corner), in physical desktop pixels.
mvp::DesktopRect ToPhysical(const ScreenMonitor& screen, const QRect& local);

/// The inverse of ToPhysical().
QRect ToLocal(const ScreenMonitor& screen, const mvp::DesktopRect& physical);

}  // namespace desktop_mapping

#endif  // MVP_APP_DESKTOP_MAPPING_H_
