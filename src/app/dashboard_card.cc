#include "dashboard_card.h"

#include <QEnterEvent>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "app_theme.h"

namespace {
constexpr int kRestBlur = 10;
constexpr int kHoverBlur = 20;
}  // namespace

DashboardCard::DashboardCard(QString title, QString description,
                            ThumbnailPainter thumbnail_painter, QWidget* parent)
    : QWidget(parent),
      title_(std::move(title)),
      description_(std::move(description)),
      thumbnail_painter_(std::move(thumbnail_painter)) {
    setFixedSize(ui_theme::kCardWidth, ui_theme::kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    shadow_effect_ = new QGraphicsDropShadowEffect(this);
    shadow_effect_->setBlurRadius(kRestBlur);
    shadow_effect_->setOffset(0, 2);
    shadow_effect_->setColor(QColor(0, 0, 0, 40));
    setGraphicsEffect(shadow_effect_);
}

void DashboardCard::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(0, hovered_ ? -2 : 0);

    QRectF body_rect(0, 0, width(), height() - 2);
    QPainterPath body_path;
    body_path.addRoundedRect(body_rect, ui_theme::kCardCornerRadius, ui_theme::kCardCornerRadius);
    painter.fillPath(body_path, Qt::white);
    painter.setPen(QPen(ui_theme::kBorder, 1));
    painter.drawPath(body_path);

    painter.setClipPath(body_path);
    PaintThumbnail(painter);
    painter.setClipping(false);

    PaintTextArea(painter);
}

void DashboardCard::PaintThumbnail(QPainter& painter) const {
    QRectF thumb_rect(0, 0, width(), ui_theme::kCardThumbnailHeight);
    painter.fillRect(thumb_rect, ui_theme::kAccentSoft);
    if (!thumbnail_painter_) {
        return;
    }
    QRectF glyph_rect = thumb_rect.adjusted(thumb_rect.width() * 0.32, thumb_rect.height() * 0.22,
                                            -thumb_rect.width() * 0.32,
                                            -thumb_rect.height() * 0.22);
    painter.setPen(QPen(ui_theme::kAccent, 2));
    painter.setBrush(ui_theme::kAccent);
    thumbnail_painter_(painter, glyph_rect);
}

void DashboardCard::PaintTextArea(QPainter& painter) const {
    QRectF text_rect(16, ui_theme::kCardThumbnailHeight + 10, width() - 32,
                     height() - ui_theme::kCardThumbnailHeight - 18);

    QFont title_font = painter.font();
    title_font.setPointSize(12);
    title_font.setBold(true);
    painter.setFont(title_font);
    painter.setPen(ui_theme::kTextPrimary);
    QRectF title_rect(text_rect.left(), text_rect.top(), text_rect.width(), 22);
    painter.drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter, title_);

    QFont desc_font = painter.font();
    desc_font.setPointSize(10);
    desc_font.setBold(false);
    painter.setFont(desc_font);
    painter.setPen(ui_theme::kTextSecondary);
    QRectF desc_rect(text_rect.left(), title_rect.bottom() + 6, text_rect.width(),
                     text_rect.height() - title_rect.height() - 6);
    painter.drawText(desc_rect, Qt::AlignLeft | Qt::TextWordWrap, description_);
}

void DashboardCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit Clicked();
    }
    QWidget::mousePressEvent(event);
}

void DashboardCard::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    hovered_ = true;
    shadow_effect_->setBlurRadius(kHoverBlur);
    shadow_effect_->setOffset(0, 6);
    shadow_effect_->setColor(QColor(0, 0, 0, 56));
    update();
}

void DashboardCard::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    hovered_ = false;
    shadow_effect_->setBlurRadius(kRestBlur);
    shadow_effect_->setOffset(0, 2);
    shadow_effect_->setColor(QColor(0, 0, 0, 40));
    update();
}
