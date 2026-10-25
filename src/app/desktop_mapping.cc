#include "desktop_mapping.h"

#include <cmath>

#include <QGuiApplication>
#include <QScreen>

#include <spdlog/spdlog.h>

namespace desktop_mapping {
namespace {

// Qt 6 names Windows screens after the monitor model, not the GDI device,
// so screens are matched by position: Qt keeps each screen's origin in
// physical pixels and scales only its size (QHighDpi::fromNativeScreenGeometry).
bool SameMonitor(const QScreen* screen, const mvp::MonitorInfo& monitor) {
    if (screen->name().toStdString() == monitor.id) {
        return true;
    }
    const QRect geometry = screen->geometry();
    const double dpr = screen->devicePixelRatio();
    const mvp::DesktopRect& bounds = monitor.bounds;
    return geometry.x() == bounds.x && geometry.y() == bounds.y &&
           std::abs(geometry.width() * dpr - bounds.width) <= 2.0 &&
           std::abs(geometry.height() * dpr - bounds.height) <= 2.0;
}

}  // namespace

std::vector<ScreenMonitor> MatchScreens() {
    const std::vector<mvp::MonitorInfo> monitors = mvp::CaptureDevices::Monitors();
    std::vector<ScreenMonitor> matched;
    for (QScreen* screen : QGuiApplication::screens()) {
        for (const mvp::MonitorInfo& monitor : monitors) {
            if (SameMonitor(screen, monitor)) {
                matched.push_back({screen, monitor});
                break;
            }
        }
    }
    if (matched.size() != QGuiApplication::screens().size()) {
        SPDLOG_WARN("desktop_mapping: matched {} of {} screens to monitors", matched.size(),
                    QGuiApplication::screens().size());
    }
    return matched;
}

std::optional<ScreenMonitor> ScreenAt(const mvp::DesktopRect& physical) {
    const int cx = physical.x + physical.width / 2;
    const int cy = physical.y + physical.height / 2;
    for (const ScreenMonitor& candidate : MatchScreens()) {
        const mvp::DesktopRect& b = candidate.monitor.bounds;
        if (cx >= b.x && cx < b.x + b.width && cy >= b.y && cy < b.y + b.height) {
            return candidate;
        }
    }
    return std::nullopt;
}

QRect ToLogical(const mvp::DesktopRect& physical) {
    const std::optional<ScreenMonitor> screen = ScreenAt(physical);
    if (!screen) {
        return {};
    }
    return ToLocal(*screen, physical).translated(screen->screen->geometry().topLeft());
}

mvp::DesktopRect ToPhysical(const ScreenMonitor& screen, const QRect& local) {
    const double dpr = screen.screen->devicePixelRatio();
    const mvp::DesktopRect& origin = screen.monitor.bounds;
    const int left = static_cast<int>(std::lround(local.left() * dpr));
    const int top = static_cast<int>(std::lround(local.top() * dpr));
    const int right = static_cast<int>(std::lround((local.left() + local.width()) * dpr));
    const int bottom = static_cast<int>(std::lround((local.top() + local.height()) * dpr));
    return {origin.x + left, origin.y + top, right - left, bottom - top};
}

QRect ToLocal(const ScreenMonitor& screen, const mvp::DesktopRect& physical) {
    const double dpr = screen.screen->devicePixelRatio();
    const mvp::DesktopRect& origin = screen.monitor.bounds;
    const int left = static_cast<int>(std::lround((physical.x - origin.x) / dpr));
    const int top = static_cast<int>(std::lround((physical.y - origin.y) / dpr));
    const int right = static_cast<int>(std::lround((physical.x + physical.width - origin.x) / dpr));
    const int bottom = static_cast<int>(std::lround((physical.y + physical.height - origin.y) / dpr));
    return {left, top, right - left, bottom - top};
}

}  // namespace desktop_mapping
