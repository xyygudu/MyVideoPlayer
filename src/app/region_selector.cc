#include "region_selector.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include "app_theme.h"
#include "desktop_mapping.h"

namespace {

constexpr int kClickTolerance = 4;  // Logical px: a press and release this close is a click
constexpr int kMinSelection = 16;
const QColor kDim{0, 0, 0, 110};
const QColor kLabelBackground{0, 0, 0, 170};

bool Contains(const mvp::DesktopRect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

// The rectangle between two corner points; unlike QRect(QPoint, QPoint) the
// second point is excluded, so dragging 400 px selects 400 px.
QRect Span(const QPoint& a, const QPoint& b) {
    return {QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
            QSize(std::abs(b.x() - a.x()), std::abs(b.y() - a.y()))};
}

void PaintTag(QPainter& painter, const QPoint& anchor, const QString& text) {
    const QFontMetrics metrics(painter.font());
    const QRect box(anchor, QSize(metrics.horizontalAdvance(text) + 16, metrics.height() + 8));
    painter.setPen(Qt::NoPen);
    painter.setBrush(kLabelBackground);
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
}

}  // namespace

/// One screen's part of the selector; reports through a callback.
class SelectionOverlay : public QWidget {
  public:
    using Done = std::function<void(std::optional<mvp::DesktopRect>)>;

    SelectionOverlay(desktop_mapping::ScreenMonitor screen, const std::vector<mvp::WindowInfo>* windows, Done done);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    /// The topmost window under `local`, clipped to this screen.
    QRect WindowAt(const QPoint& local) const;
    QRect Highlighted() const { return dragging_ ? selection_ : hovered_; }

    desktop_mapping::ScreenMonitor screen_;
    const std::vector<mvp::WindowInfo>* windows_;
    Done done_;
    QPixmap background_;
    QPoint press_;
    QRect selection_;
    QRect hovered_;
    bool dragging_{false};
};

SelectionOverlay::SelectionOverlay(desktop_mapping::ScreenMonitor screen, const std::vector<mvp::WindowInfo>* windows,
                                   Done done)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      screen_(std::move(screen)),
      windows_(windows),
      done_(std::move(done)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    background_ = screen_.screen->grabWindow(0);  // Frozen before the overlay covers it
    create();
    windowHandle()->setScreen(screen_.screen);
    setGeometry(screen_.screen->geometry());
}

void SelectionOverlay::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.drawPixmap(rect(), background_);
    const QRect area = Highlighted();
    painter.setClipRegion(QRegion(rect()).subtracted(QRegion(area)));
    painter.fillRect(rect(), kDim);
    painter.setClipping(false);
    if (!area.isEmpty()) {
        painter.setPen(QPen(ui_theme::kAccent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area.adjusted(1, 1, -1, -1));
        const mvp::DesktopRect physical = desktop_mapping::ToPhysical(screen_, area);
        const QPoint anchor = area.top() > 32 ? area.topLeft() - QPoint(0, 30) : area.topLeft() + QPoint(6, 6);
        PaintTag(painter, anchor, QStringLiteral("%1 \u00D7 %2").arg(physical.width).arg(physical.height));
    }
    // 拖动鼠标框选录制区域，单击选择窗口，Esc 取消
    const QString hint = QStringLiteral("\u62D6\u52A8\u9F20\u6807\u6846\u9009\u5F55\u5236\u533A\u57DF\uFF0C"
                                        "\u5355\u51FB\u9009\u62E9\u7A97\u53E3\uFF0CEsc \u53D6\u6D88");
    const int hint_width = QFontMetrics(painter.font()).horizontalAdvance(hint) + 16;
    PaintTag(painter, QPoint((width() - hint_width) / 2, 24), hint);
}

void SelectionOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        done_(std::nullopt);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    press_ = event->position().toPoint();
    selection_ = QRect(press_, QSize(0, 0));
    dragging_ = true;
    update();
}

void SelectionOverlay::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (dragging_) {
        selection_ = Span(press_, pos) & rect();
    } else {
        hovered_ = WindowAt(pos);
    }
    update();
}

void SelectionOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !dragging_) {
        return;
    }
    dragging_ = false;
    const QPoint pos = event->position().toPoint();
    const bool click = (pos - press_).manhattanLength() <= kClickTolerance;
    const QRect chosen = click ? WindowAt(pos) : selection_;
    if (chosen.width() < kMinSelection || chosen.height() < kMinSelection) {
        selection_ = QRect();  // Too small to record: keep selecting
        update();
        return;
    }
    done_(desktop_mapping::ToPhysical(screen_, chosen));
}

void SelectionOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        done_(std::nullopt);
        return;
    }
    QWidget::keyPressEvent(event);
}

QRect SelectionOverlay::WindowAt(const QPoint& local) const {
    const mvp::DesktopRect point = desktop_mapping::ToPhysical(screen_, QRect(local, QSize(1, 1)));
    for (const mvp::WindowInfo& window : *windows_) {
        if (Contains(window.bounds, point.x, point.y)) {
            return desktop_mapping::ToLocal(screen_, window.bounds) & rect();
        }
    }
    return rect();  // Over no window: the whole screen
}

// --- RegionSelector ---

RegionSelector::RegionSelector(QObject* parent) : QObject(parent) {}

RegionSelector::~RegionSelector() { CloseOverlays(); }

void RegionSelector::Start() {
    CloseOverlays();
    windows_ = mvp::CaptureDevices::Windows();
    const std::vector<desktop_mapping::ScreenMonitor> screens = desktop_mapping::MatchScreens();
    if (screens.empty()) {
        emit Cancelled();
        return;
    }
    for (const desktop_mapping::ScreenMonitor& screen : screens) {
        auto* overlay = new SelectionOverlay(screen, &windows_, [this](std::optional<mvp::DesktopRect> rect) {
            Finish(rect);
        });
        overlays_.emplace_back(overlay);
        overlay->show();
    }
    // Esc goes to the overlay under the cursor.
    for (const QPointer<SelectionOverlay>& overlay : overlays_) {
        if (overlay->geometry().contains(QCursor::pos())) {
            overlay->raise();
            overlay->activateWindow();
            overlay->setFocus();
        }
    }
}

void RegionSelector::Finish(std::optional<mvp::DesktopRect> rect) {
    if (overlays_.empty()) {
        return;  // Already finished (events still queued for another overlay)
    }
    CloseOverlays();
    if (rect) {
        emit Selected(*rect);
    } else {
        emit Cancelled();
    }
}

void RegionSelector::CloseOverlays() {
    for (const QPointer<SelectionOverlay>& overlay : overlays_) {
        if (overlay) {
            overlay->close();  // Deleted later (WA_DeleteOnClose)
        }
    }
    overlays_.clear();
}
