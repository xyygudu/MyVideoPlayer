#include "capture_area_section.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>

#include <spdlog/spdlog.h>

#include "region_selector.h"

namespace {

// Lets the main window's hide animation end before the screens are grabbed.
constexpr int kStepAsideMs = 250;
constexpr int kMaxTitleLength = 60;

QString MonitorName(const mvp::MonitorInfo& monitor, int index) {
    QString name = QStringLiteral("\u663E\u793A\u5668 %1").arg(index + 1);  // 显示器 N
    if (monitor.primary) {
        name += QStringLiteral("\uFF08\u4E3B\uFF09");  // （主）
    }
    return name + QStringLiteral("  %1 \u00D7 %2").arg(monitor.bounds.width).arg(monitor.bounds.height);
}

QString RegionText(const mvp::DesktopRect& rect) {
    // W × H，位置 (x, y)
    return QStringLiteral("%1 \u00D7 %2\uFF0C\u4F4D\u7F6E (%3, %4)")
        .arg(rect.width)
        .arg(rect.height)
        .arg(rect.x)
        .arg(rect.y);
}

}  // namespace

CaptureAreaSection::CaptureAreaSection(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    monitor_radio_ = new QRadioButton(QStringLiteral("\u5168\u5C4F"), this);            // 全屏
    region_radio_ = new QRadioButton(QStringLiteral("\u9009\u5B9A\u533A\u57DF"), this);  // 选定区域
    window_radio_ = new QRadioButton(QStringLiteral("\u7A97\u53E3"), this);             // 窗口
    auto* group = new QButtonGroup(this);
    monitor_combo_ = new QComboBox(this);
    grid->addWidget(monitor_radio_, 0, 0);
    grid->addWidget(monitor_combo_, 0, 1);
    grid->addWidget(region_radio_, 1, 0);
    grid->addWidget(BuildRegionRow(), 1, 1);
    grid->addWidget(window_radio_, 2, 0);
    grid->addWidget(BuildWindowRow(), 2, 1);
    grid->setColumnStretch(1, 1);
    for (QRadioButton* radio : {monitor_radio_, region_radio_, window_radio_}) {
        group->addButton(radio);
        connect(radio, &QRadioButton::toggled, this, &CaptureAreaSection::UpdateEnabled);
    }
    monitor_radio_->setChecked(true);

    selector_ = new RegionSelector(this);
    connect(selector_, &RegionSelector::Selected, this, [this](const mvp::DesktopRect& rect) { OnRegionPicked(rect); });
    connect(selector_, &RegionSelector::Cancelled, this, [this] { OnRegionPicked(std::nullopt); });
    Refresh();
    ShowRegion();
}

QWidget* CaptureAreaSection::BuildRegionRow() {
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    pick_region_button_ = new QPushButton(QStringLiteral("\u9009\u62E9\u533A\u57DF\u2026"), row);  // 选择区域…
    pick_region_button_->setObjectName(QStringLiteral("SecondaryButton"));
    connect(pick_region_button_, &QPushButton::clicked, this, &CaptureAreaSection::PickRegion);
    region_label_ = new QLabel(row);
    layout->addWidget(pick_region_button_);
    layout->addWidget(region_label_, 1);
    return row;
}

QWidget* CaptureAreaSection::BuildWindowRow() {
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    window_combo_ = new QComboBox(row);
    window_combo_->setMinimumWidth(260);
    refresh_windows_button_ = new QPushButton(QStringLiteral("\u5237\u65B0"), row);  // 刷新
    refresh_windows_button_->setObjectName(QStringLiteral("SecondaryButton"));
    connect(refresh_windows_button_, &QPushButton::clicked, this, &CaptureAreaSection::RefreshWindows);
    layout->addWidget(window_combo_, 1);
    layout->addWidget(refresh_windows_button_);
    return row;
}

bool CaptureAreaSection::Fill(mvp::VideoCaptureOptions* options, QString* problem) const {
    options->mode = Mode();
    switch (options->mode) {
        case mvp::CaptureMode::kMonitor:
            options->monitor_id = monitor_combo_->currentData().toString().toStdString();
            return true;
        case mvp::CaptureMode::kRegion:
            if (!region_) {
                // 请先选择要录制的区域
                *problem = QStringLiteral("\u8BF7\u5148\u9009\u62E9\u8981\u5F55\u5236\u7684\u533A\u57DF");
                return false;
            }
            options->region = *region_;
            return true;
        case mvp::CaptureMode::kWindow:
            options->window_id = WindowId();
            if (!mvp::CaptureDevices::WindowBounds(options->window_id)) {
                // 所选窗口已关闭或最小化，请刷新后重新选择
                *problem = QStringLiteral("\u6240\u9009\u7A97\u53E3\u5DF2\u5173\u95ED\u6216\u6700\u5C0F\u5316\uFF0C"
                                          "\u8BF7\u5237\u65B0\u540E\u91CD\u65B0\u9009\u62E9");
                return false;
            }
            return true;
    }
    return false;
}

mvp::DesktopRect CaptureAreaSection::Area() const {
    switch (Mode()) {
        case mvp::CaptureMode::kMonitor: {
            const std::string id = monitor_combo_->currentData().toString().toStdString();
            for (const mvp::MonitorInfo& monitor : monitors_) {
                if (monitor.id == id) return monitor.bounds;
            }
            return {};
        }
        case mvp::CaptureMode::kRegion:
            return region_.value_or(mvp::DesktopRect{});
        case mvp::CaptureMode::kWindow:
            return mvp::CaptureDevices::WindowBounds(WindowId()).value_or(mvp::DesktopRect{});
    }
    return {};
}

mvp::CaptureMode CaptureAreaSection::Mode() const {
    if (region_radio_->isChecked()) return mvp::CaptureMode::kRegion;
    if (window_radio_->isChecked()) return mvp::CaptureMode::kWindow;
    return mvp::CaptureMode::kMonitor;
}

uint64_t CaptureAreaSection::WindowId() const { return window_combo_->currentData().toULongLong(); }

void CaptureAreaSection::Refresh() {
    RefreshMonitors();
    RefreshWindows();
    UpdateEnabled();
}

void CaptureAreaSection::RefreshMonitors() {
    const QString selected = monitor_combo_->currentData().toString();
    monitors_ = mvp::CaptureDevices::Monitors();
    const QSignalBlocker blocker(monitor_combo_);
    monitor_combo_->clear();
    int primary = 0;
    for (size_t i = 0; i < monitors_.size(); ++i) {
        const QString id = QString::fromStdString(monitors_[i].id);
        monitor_combo_->addItem(MonitorName(monitors_[i], static_cast<int>(i)), id);
        if (monitors_[i].primary) primary = static_cast<int>(i);
    }
    const int index = monitor_combo_->findData(selected);
    monitor_combo_->setCurrentIndex(index >= 0 ? index : primary);
}

void CaptureAreaSection::RefreshWindows() {
    const QString selected = window_combo_->currentText();
    windows_ = mvp::CaptureDevices::Windows();
    const QSignalBlocker blocker(window_combo_);
    window_combo_->clear();
    for (const mvp::WindowInfo& window : windows_) {
        QString title = QString::fromStdString(window.title);
        if (title.size() > kMaxTitleLength) {
            title = title.left(kMaxTitleLength) + QStringLiteral("\u2026");
        }
        window_combo_->addItem(title, QVariant::fromValue<qulonglong>(window.id));
    }
    const int index = window_combo_->findText(selected);
    window_combo_->setCurrentIndex(index >= 0 ? index : 0);
}

void CaptureAreaSection::PickRegion() {
    SPDLOG_INFO("UI: picking the recording region");
    window()->hide();
    QTimer::singleShot(kStepAsideMs, selector_, &RegionSelector::Start);
}

void CaptureAreaSection::OnRegionPicked(std::optional<mvp::DesktopRect> region) {
    QWidget* main_window = window();
    main_window->show();
    main_window->raise();
    main_window->activateWindow();
    if (!region) {
        SPDLOG_INFO("UI: region picking cancelled");
        return;
    }
    SPDLOG_INFO("UI: region {}x{} at ({},{}) picked", region->width, region->height, region->x, region->y);
    region_ = region;
    region_radio_->setChecked(true);
    ShowRegion();
}

void CaptureAreaSection::ShowRegion() {
    // 尚未选择
    region_label_->setText(region_ ? RegionText(*region_) : QStringLiteral("\u5C1A\u672A\u9009\u62E9"));
}

void CaptureAreaSection::UpdateEnabled() {
    monitor_combo_->setEnabled(monitor_radio_->isChecked());
    pick_region_button_->setEnabled(region_radio_->isChecked());
    region_label_->setEnabled(region_radio_->isChecked());
    window_combo_->setEnabled(window_radio_->isChecked());
    refresh_windows_button_->setEnabled(window_radio_->isChecked());
}

void CaptureAreaSection::LoadSettings(const QSettings& settings) {
    const int monitor = monitor_combo_->findData(settings.value("recorder/area/monitor").toString());
    if (monitor >= 0) {
        monitor_combo_->setCurrentIndex(monitor);
    }
    const QRect region = settings.value("recorder/area/region").toRect();
    if (region.width() > 0 && region.height() > 0) {
        region_ = mvp::DesktopRect{region.x(), region.y(), region.width(), region.height()};
        ShowRegion();
    }
    const int window = window_combo_->findText(settings.value("recorder/area/window").toString());
    if (window >= 0) {
        window_combo_->setCurrentIndex(window);
    }
    const int mode = settings.value("recorder/area/mode", 0).toInt();
    (mode == 1 && region_ ? region_radio_ : mode == 2 ? window_radio_ : monitor_radio_)->setChecked(true);
}

void CaptureAreaSection::SaveSettings(QSettings& settings) const {
    settings.setValue("recorder/area/mode", static_cast<int>(Mode()));
    settings.setValue("recorder/area/monitor", monitor_combo_->currentData().toString());
    if (region_) {
        settings.setValue("recorder/area/region", QRect(region_->x, region_->y, region_->width, region_->height));
    }
    settings.setValue("recorder/area/window", window_combo_->currentText());
}
