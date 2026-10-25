#include "level_meter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "app_theme.h"

namespace {

constexpr float kFloorDb = -60.0f;
constexpr float kFallPerUpdate = 0.08f;  // Fraction of the scale per SetPeak()
const QColor kLoudColor{"#F5A623"};
const QColor kClipColor{"#E5484D"};

float ToScale(float peak) {
    if (peak <= 0.0f) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

}  // namespace

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    setFixedHeight(8);
    setMinimumWidth(60);
}

void LevelMeter::SetPeak(float peak) {
    level_ = std::max(ToScale(peak), level_ - kFallPerUpdate);
    update();
}

void LevelMeter::Reset() {
    level_ = 0.0f;
    update();
}

void LevelMeter::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF bar = rect();
    painter.setBrush(ui_theme::kBorder);
    painter.drawRoundedRect(bar, 3, 3);
    if (level_ <= 0.0f) {
        return;
    }
    // Accent up to -12 dB, amber up to -3 dB, red above.
    const QColor color = level_ > 0.95f ? kClipColor : (level_ > 0.8f ? kLoudColor : ui_theme::kAccent);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * level_, bar.height()), 3, 3);
}
