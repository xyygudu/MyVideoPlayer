#include "video_widget.h"

#include <QPainter>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background-color: black;");
}

void VideoWidget::UpdateFrame(const QImage& image) {
    QMutexLocker locker(&mutex_);
    current_frame_ = image.copy();
    update();
}

void VideoWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    QMutexLocker locker(&mutex_);
    if (current_frame_.isNull()) return;

    // Scale keeping aspect ratio, centered
    QSize scaled_size = current_frame_.size().scaled(size(), Qt::KeepAspectRatio);
    int x = (width() - scaled_size.width()) / 2;
    int y = (height() - scaled_size.height()) / 2;

    painter.drawImage(QRect(x, y, scaled_size.width(), scaled_size.height()), current_frame_);
}
