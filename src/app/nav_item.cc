#include "nav_item.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>

#include "app_theme.h"

NavItem::NavItem(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    setFixedHeight(ui_theme::kNavItemHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void NavItem::SetSelected(bool selected) {
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    update();
}

void NavItem::SetIcon(const QIcon& icon) {
    icon_ = icon;
    update();
}

void NavItem::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (selected_) {
        painter.fillRect(rect(), ui_theme::kAccentSoft);
        painter.fillRect(QRect(0, 0, ui_theme::kNavSelectedBarWidth, height()), ui_theme::kAccent);
    } else if (underMouse()) {
        painter.fillRect(rect(), ui_theme::kHoverTint);
    }

    const int icon_left = ui_theme::kNavItemLeftPadding;
    const int text_left = icon_left + ui_theme::kNavIconSize + ui_theme::kNavIconTextGap;

    if (!icon_.isNull()) {
        QRect icon_rect(icon_left, (height() - ui_theme::kNavIconSize) / 2, ui_theme::kNavIconSize,
                       ui_theme::kNavIconSize);
        icon_.paint(&painter, icon_rect);
    }

    painter.setPen(selected_ ? ui_theme::kAccent : ui_theme::kTextPrimary);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    QRect text_rect = rect().adjusted(text_left, 0, -8, 0);
    painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, text_);
}

void NavItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit Clicked();
    }
    QWidget::mousePressEvent(event);
}

void NavItem::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    update();
}

void NavItem::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    update();
}
