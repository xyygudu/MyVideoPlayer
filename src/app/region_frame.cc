#include "region_frame.h"

#include <QPainter>

#include "desktop_mapping.h"
#include "shared_ui.h"

namespace {

constexpr int kBorder = 3;  // Logical px, drawn outside the recorded area
const QColor kRecordingColor{"#E5484D"};
const QColor kPausedColor{"#F5A623"};

}  // namespace

RegionFrame::RegionFrame()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                           Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void RegionFrame::ShowAround(const mvp::DesktopRect& area) {
    const QRect logical = desktop_mapping::ToLogical(area);
    if (!logical.isValid()) {
        hide();
        return;
    }
    setGeometry(logical.adjusted(-kBorder, -kBorder, kBorder, kBorder));
    if (!isVisible()) {
        show();
        shared_ui::ExcludeFromCapture(this);
    }
}

void RegionFrame::SetPaused(bool paused) {
    paused_ = paused;
    update();
}

void RegionFrame::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    const QColor color = paused_ ? kPausedColor : kRecordingColor;
    const QRect outer = rect();
    painter.fillRect(QRect(0, 0, outer.width(), kBorder), color);
    painter.fillRect(QRect(0, outer.height() - kBorder, outer.width(), kBorder), color);
    painter.fillRect(QRect(0, kBorder, kBorder, outer.height() - 2 * kBorder), color);
    painter.fillRect(QRect(outer.width() - kBorder, kBorder, kBorder, outer.height() - 2 * kBorder), color);
}
