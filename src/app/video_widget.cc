#include "video_widget.h"

#include <QResizeEvent>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_NativeWindow, true);

    // WA_PaintOnScreen is OFF by default — Qt's backing store fills the
    // widget surface, no stale page content can leak through. When a video
    // starts playing, SetVideoPlaying(true) turns it ON so SDL renders
    // directly without Qt painting over SDL's frames.
}

void VideoWidget::SetResizeCallback(ResizeCallback cb) { resize_cb_ = std::move(cb); }

void VideoWidget::SetVideoPlaying(bool playing) {
    has_video_ = playing;
    setAttribute(Qt::WA_PaintOnScreen, playing);
    update();
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (resize_cb_) {
        resize_cb_(event->size().width(), event->size().height());
    }
}
