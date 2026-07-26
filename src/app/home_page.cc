#include "home_page.h"

#include <QPainter>
#include <QPixmap>

#include "dashboard_card.h"
#include "flow_layout.h"
#include "app_theme.h"

namespace {

// Scales |pix| to fill |target|, preserving aspect ratio, then center-crops
// so no letterboxing appears. Equivalent to CSS "object-fit: cover".
void PaintCoverImage(QPainter& painter, const QRectF& target, const QPixmap& pix) {
    if (pix.isNull()) return;
    qreal scale = qMax(target.width() / pix.width(), target.height() / pix.height());
    qreal src_w = target.width() / scale;
    qreal src_h = target.height() / scale;
    QRectF src((pix.width() - src_w) / 2.0, (pix.height() - src_h) / 2.0, src_w, src_h);
    painter.drawPixmap(target, pix, src);
}

}  // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ui_theme::kBgContent);
    setPalette(pal);
    SetupUi();
}

void HomePage::SetupUi() {
    auto* layout = new FlowLayout(this, ui_theme::kContentPadding, 20, 20);

    auto player_thumbnail = [](QPainter& painter, const QRectF& r) {
        static const QPixmap pix(QStringLiteral(":/images/player_cover.png"));
        PaintCoverImage(painter, r, pix);
    };
    auto* player_card = new DashboardCard(
        QStringLiteral("\u64AD\u653E\u5668"),  // 播放器
        QStringLiteral("\u64AD\u653E\u672C\u5730\u89C6\u9891\u6587\u4EF6\uFF0C\u652F\u6301"
                       "\u97F3\u89C6\u9891\u540C\u6B65\u3001\u8FDB\u5EA6\u62D6\u62FD\u4E0E"
                       "\u89C6\u9891\u7279\u6548\u3002"),
        std::move(player_thumbnail), this);
    connect(player_card, &DashboardCard::Clicked, this,
            [this] { emit NavigateRequested(NavigationBar::Page::kPlayer); });
    layout->addWidget(player_card);

    auto converter_thumbnail = [](QPainter& painter, const QRectF& r) {
        static const QPixmap pix(QStringLiteral(":/images/trancoder_cover.png"));
        PaintCoverImage(painter, r, pix);
    };
    auto* transcoder_card = new DashboardCard(
        QStringLiteral("\u8F6C\u7801\u5668"),  // 转码器
        QStringLiteral("\u5C06\u89C6\u9891\u8F6C\u7801\u4E3A\u5176\u4ED6\u683C\u5F0F\uFF0C"
                       "\u652F\u6301\u753B\u8D28\u9884\u8BBE\u4E0E\u7F16\u89E3\u7801\u53C2"
                       "\u6570\u8C03\u6574\u3002"),
        std::move(converter_thumbnail), this);
    connect(transcoder_card, &DashboardCard::Clicked, this,
            [this] { emit NavigateRequested(NavigationBar::Page::kTranscoder); });
    layout->addWidget(transcoder_card);
}
