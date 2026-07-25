#include "home_page.h"

#include <QPainter>
#include <QPainterPath>

#include "dashboard_card.h"
#include "flow_layout.h"
#include "app_theme.h"

namespace {

void PaintPlayGlyph(QPainter& painter, const QRectF& r) {
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right(), r.center().y());
    path.lineTo(r.left(), r.bottom());
    path.closeSubpath();
    painter.drawPath(path);
}

void PaintConvertGlyph(QPainter& painter, const QRectF& r) {
    // Two opposing arcs suggesting format conversion (recycle-like glyph).
    painter.setBrush(Qt::NoBrush);
    QRectF top_arc(r.left(), r.top(), r.width(), r.height() * 0.55);
    QRectF bottom_arc(r.left(), r.center().y() - r.height() * 0.05, r.width(), r.height() * 0.55);
    painter.drawArc(top_arc, 20 * 16, 140 * 16);
    painter.drawArc(bottom_arc, (20 + 180) * 16, 140 * 16);
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

    auto* player_card = new DashboardCard(
        QStringLiteral("\u64AD\u653E\u5668"),  // 播放器
        QStringLiteral("\u64AD\u653E\u672C\u5730\u89C6\u9891\u6587\u4EF6\uFF0C\u652F\u6301"
                       "\u97F3\u89C6\u9891\u540C\u6B65\u3001\u8FDB\u5EA6\u62D6\u62FD\u4E0E"
                       "\u89C6\u9891\u7279\u6548\u3002"),
        &PaintPlayGlyph, this);
    connect(player_card, &DashboardCard::Clicked, this,
            [this] { emit NavigateRequested(NavigationBar::Page::kPlayer); });
    layout->addWidget(player_card);

    auto* transcoder_card = new DashboardCard(
        QStringLiteral("\u8F6C\u7801\u5668"),  // 转码器
        QStringLiteral("\u5C06\u89C6\u9891\u8F6C\u7801\u4E3A\u5176\u4ED6\u683C\u5F0F\uFF0C"
                       "\u652F\u6301\u753B\u8D28\u9884\u8BBE\u4E0E\u7F16\u89E3\u7801\u53C2"
                       "\u6570\u8C03\u6574\u3002"),
        &PaintConvertGlyph, this);
    connect(transcoder_card, &DashboardCard::Clicked, this,
            [this] { emit NavigateRequested(NavigationBar::Page::kTranscoder); });
    layout->addWidget(transcoder_card);
}
