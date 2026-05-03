#include "video_widget.h"

#include <QResizeEvent>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Critical: tell Qt not to paint this widget — SDL owns the surface
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setStyleSheet("background-color: black;");
}

void VideoWidget::SetResizeCallback(ResizeCallback cb) { resize_cb_ = std::move(cb); }

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (resize_cb_) {
        resize_cb_(event->size().width(), event->size().height());
    }
}
