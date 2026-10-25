#include "recording_toolbar.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include "icon_button.h"
#include "shared_ui.h"

namespace {

const QColor kBackground{32, 34, 38, 235};
const QColor kRecordingDot{"#E5484D"};
const QColor kPausedDot{"#F5A623"};
constexpr int kDotSize = 10;
constexpr int kButtonSize = 28;
constexpr int kIconSize = 14;

IconButton* MakeButton(IconButton::IconKind kind, const QString& icon, const QString& tip, QWidget* parent) {
    auto* button = new IconButton(kind, parent);
    button->setFixedSize(kButtonSize, kButtonSize);
    button->SetCustomIcon(shared_ui::LoadSvgIcon(icon, kIconSize, Qt::white));
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

}  // namespace

RecordingToolbar::RecordingToolbar()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14 + kDotSize + 8, 6, 8, 6);
    layout->setSpacing(6);

    time_label_ = new QLabel(QStringLiteral("00:00:00"), this);
    QFont font = time_label_->font();
    font.setPointSize(11);
    font.setBold(true);
    font.setStyleHint(QFont::Monospace);
    time_label_->setFont(font);
    time_label_->setStyleSheet(QStringLiteral("color: white;"));
    layout->addWidget(time_label_);
    layout->addSpacing(6);

    // 暂停 / 停止 (hotkeys shown by the page's tooltips)
    pause_button_ = MakeButton(IconButton::IconKind::kPause, QStringLiteral(":/icons/pause.svg"),
                               QStringLiteral("\u6682\u505C"), this);
    stop_button_ = MakeButton(IconButton::IconKind::kPause, QStringLiteral(":/icons/stop.svg"),
                              QStringLiteral("\u505C\u6B62\u5F55\u5236"), this);
    connect(pause_button_, &QAbstractButton::clicked, this, &RecordingToolbar::PauseResumeClicked);
    connect(stop_button_, &QAbstractButton::clicked, this, &RecordingToolbar::StopClicked);
    layout->addWidget(pause_button_);
    layout->addWidget(stop_button_);
}

void RecordingToolbar::ShowOn(QScreen* screen) {
    adjustSize();
    if (screen) {
        const QRect area = screen->availableGeometry();
        move(area.center().x() - width() / 2, area.top() + 12);
    }
    show();
    shared_ui::ExcludeFromCapture(this);
}

void RecordingToolbar::SetElapsed(double seconds) { time_label_->setText(shared_ui::FormatDuration(seconds)); }

void RecordingToolbar::SetPaused(bool paused) {
    paused_ = paused;
    // 继续 / 暂停
    pause_button_->SetCustomIcon(shared_ui::LoadSvgIcon(
        paused ? QStringLiteral(":/icons/play.svg") : QStringLiteral(":/icons/pause.svg"), kIconSize, Qt::white));
    pause_button_->setToolTip(paused ? QStringLiteral("\u7EE7\u7EED") : QStringLiteral("\u6682\u505C"));
    update();
}

void RecordingToolbar::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBackground);
    painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    painter.setBrush(paused_ ? kPausedDot : kRecordingDot);
    painter.drawEllipse(QRectF(14, (height() - kDotSize) / 2.0, kDotSize, kDotSize));
}

void RecordingToolbar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void RecordingToolbar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - drag_offset_);
    }
}
