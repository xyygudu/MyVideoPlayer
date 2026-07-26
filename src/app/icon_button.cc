#include "icon_button.h"

#include <algorithm>

#include <QPainter>
#include <QPainterPath>

#include "app_theme.h"

namespace {

void DrawPlay(QPainter& p, const QRectF& r) {
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right(), r.center().y());
    path.lineTo(r.left(), r.bottom());
    path.closeSubpath();
    p.drawPath(path);
}

void DrawPause(QPainter& p, const QRectF& r) {
    qreal bar_w = r.width() * 0.32;
    p.drawRect(QRectF(r.left(), r.top(), bar_w, r.height()));
    p.drawRect(QRectF(r.right() - bar_w, r.top(), bar_w, r.height()));
}

void DrawFolder(QPainter& p, const QRectF& r) {
    p.setBrush(Qt::NoBrush);
    qreal tab_w = r.width() * 0.45;
    qreal body_top = r.top() + r.height() * 0.22;
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), body_top + r.height() * 0.1);
    path.lineTo(r.left() + tab_w * 0.4, body_top + r.height() * 0.1);
    path.lineTo(r.left() + tab_w * 0.6, body_top);
    path.lineTo(r.right(), body_top);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    p.drawPath(path);
}

void DrawMinimize(QPainter& p, const QRectF& r) {
    qreal y = r.center().y();
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
}

void DrawMaximize(QPainter& p, const QRectF& r) {
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

void DrawRestore(QPainter& p, const QRectF& r) {
    p.setBrush(Qt::NoBrush);
    QRectF back(r.left() + r.width() * 0.25, r.top(), r.width() * 0.75, r.height() * 0.75);
    QRectF front(r.left(), r.top() + r.height() * 0.25, r.width() * 0.75, r.height() * 0.75);
    p.drawRect(back);
    p.drawRect(front);
}

void DrawClose(QPainter& p, const QRectF& r) {
    p.drawLine(r.topLeft(), r.bottomRight());
    p.drawLine(r.bottomLeft(), r.topRight());
}

void DrawChevronDown(QPainter& p, const QRectF& r) {
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.center().x(), r.bottom());
    path.lineTo(r.right(), r.top());
    p.drawPath(path);
}

void DrawChevronRight(QPainter& p, const QRectF& r) {
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right(), r.center().y());
    path.lineTo(r.left(), r.bottom());
    p.drawPath(path);
}

}  // namespace

IconButton::IconButton(IconKind kind, QWidget* parent)
    : QAbstractButton(parent), kind_(kind) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void IconButton::SetIconKind(IconKind kind) {
    kind_ = kind;
    update();
}

QSize IconButton::sizeHint() const { return QSize(32, 32); }

void IconButton::enterEvent(QEnterEvent* event) {
    QAbstractButton::enterEvent(event);
    update();
}

void IconButton::leaveEvent(QEvent* event) {
    QAbstractButton::leaveEvent(event);
    update();
}

void IconButton::PaintBackground(QPainter& painter) const {
    if (isDown()) {
        painter.fillRect(rect(), close_style_ ? ui_theme::kCloseHover.darker(115)
                                              : ui_theme::kHoverTint.darker(108));
    } else if (underMouse()) {
        painter.fillRect(rect(), close_style_ ? ui_theme::kCloseHover : ui_theme::kHoverTint);
    }
}

void IconButton::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    PaintBackground(painter);

    if (!custom_icon_.isNull()) {
        qreal side = std::min(width(), height()) * glyph_scale_;
        QRectF glyph_rect(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0, side, side);
        custom_icon_.paint(&painter, glyph_rect.toRect());
        return;
    }

    QColor glyph_color = (close_style_ && (underMouse() || isDown()))
                             ? Qt::white
                             : ui_theme::kTextPrimary;
    painter.setPen(QPen(glyph_color, 1.3));
    painter.setBrush(glyph_color);

    qreal side = std::min(width(), height()) * glyph_scale_;
    QRectF glyph_rect(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0, side, side);
    PaintGlyph(painter, glyph_rect);
}

void IconButton::PaintGlyph(QPainter& painter, const QRectF& r) const {
    switch (kind_) {
        case IconKind::kPlay: DrawPlay(painter, r); break;
        case IconKind::kPause: DrawPause(painter, r); break;
        case IconKind::kOpenFolder: DrawFolder(painter, r); break;
        case IconKind::kMinimize: DrawMinimize(painter, r); break;
        case IconKind::kMaximize: DrawMaximize(painter, r); break;
        case IconKind::kRestore: DrawRestore(painter, r); break;
        case IconKind::kClose: DrawClose(painter, r); break;
        case IconKind::kChevronDown: DrawChevronDown(painter, r); break;
        case IconKind::kChevronRight: DrawChevronRight(painter, r); break;
    }
}
