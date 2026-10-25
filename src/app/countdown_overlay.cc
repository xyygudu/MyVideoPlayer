#include "countdown_overlay.h"

#include <QFont>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include "shared_ui.h"

namespace {

constexpr int kSize = 160;  // Logical px
const QColor kBackground{0, 0, 0, 160};

}  // namespace

CountdownOverlay::CountdownOverlay()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                           Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(kSize, kSize);
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &CountdownOverlay::Tick);
}

void CountdownOverlay::Start(QScreen* screen, int seconds) {
    remaining_ = seconds;
    if (screen) {
        move(screen->geometry().center() - QPoint(kSize / 2, kSize / 2));
    }
    show();
    shared_ui::ExcludeFromCapture(this);
    timer_->start();
    update();
}

void CountdownOverlay::Cancel() {
    timer_->stop();
    hide();
}

void CountdownOverlay::Tick() {
    if (--remaining_ > 0) {
        update();
        return;
    }
    Cancel();
    emit Finished();
}

void CountdownOverlay::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBackground);
    painter.drawEllipse(rect());
    QFont font = painter.font();
    font.setPixelSize(kSize / 2);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, QString::number(remaining_));
}
