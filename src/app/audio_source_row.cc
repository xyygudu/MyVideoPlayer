#include "audio_source_row.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>

#include "level_meter.h"

namespace {

constexpr int kMaxVolumePercent = 200;

}  // namespace

AudioSourceRow::AudioSourceRow(mvp::AudioDeviceKind kind, const QString& title, bool enabled_by_default,
                               QWidget* parent)
    : QWidget(parent), kind_(kind) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);

    enabled_box_ = new QCheckBox(title, this);
    enabled_box_->setChecked(enabled_by_default);
    device_combo_ = new QComboBox(this);
    device_combo_->setMinimumWidth(260);
    volume_slider_ = new QSlider(Qt::Horizontal, this);
    volume_slider_->setRange(0, kMaxVolumePercent);
    volume_slider_->setValue(100);
    volume_slider_->setFixedWidth(140);
    volume_label_ = new QLabel(this);
    volume_label_->setFixedWidth(40);
    meter_ = new LevelMeter(this);

    grid->addWidget(enabled_box_, 0, 0);
    grid->addWidget(device_combo_, 0, 1, 1, 3);
    grid->addWidget(volume_slider_, 1, 1);
    grid->addWidget(volume_label_, 1, 2);
    grid->addWidget(meter_, 1, 3);
    grid->setColumnStretch(3, 1);
    grid->setColumnMinimumWidth(0, 120);

    connect(enabled_box_, &QCheckBox::toggled, this, &AudioSourceRow::UpdateEnabled);
    connect(volume_slider_, &QSlider::valueChanged, this, [this](int percent) {
        ShowVolume(percent);
        emit VolumeChanged(percent / 100.0f);
    });
    ShowVolume(volume_slider_->value());
    RefreshDevices();
    UpdateEnabled();
}

mvp::AudioCaptureOptions AudioSourceRow::Options() const {
    mvp::AudioCaptureOptions options;
    options.enabled = enabled_box_->isChecked() && device_combo_->count() > 0;
    options.device_id = device_combo_->currentData().toString().toStdString();
    options.volume = volume_slider_->value() / 100.0f;
    return options;
}

void AudioSourceRow::SetLevel(float peak) { meter_->SetPeak(peak); }

void AudioSourceRow::ResetLevel() { meter_->Reset(); }

void AudioSourceRow::RefreshDevices() {
    const QString selected = device_combo_->currentData().toString();
    const QSignalBlocker blocker(device_combo_);
    device_combo_->clear();
    const std::vector<mvp::AudioDeviceInfo> devices = mvp::CaptureDevices::AudioDevices(kind_);
    for (const mvp::AudioDeviceInfo& device : devices) {
        if (device.is_default) {
            // 系统默认（…）: follows Windows when the default device changes.
            device_combo_->insertItem(0, QStringLiteral("\u7CFB\u7EDF\u9ED8\u8BA4\uFF08%1\uFF09")
                                             .arg(QString::fromStdString(device.name)),
                                      QString());
        }
        device_combo_->addItem(QString::fromStdString(device.name), QString::fromStdString(device.id));
    }
    const int index = device_combo_->findData(selected);
    device_combo_->setCurrentIndex(index >= 0 ? index : 0);
    UpdateEnabled();
}

void AudioSourceRow::LoadSettings(const QSettings& settings, const QString& prefix) {
    enabled_box_->setChecked(settings.value(prefix + "enabled", enabled_box_->isChecked()).toBool());
    const int index = device_combo_->findData(settings.value(prefix + "device", QString()).toString());
    device_combo_->setCurrentIndex(index >= 0 ? index : 0);
    volume_slider_->setValue(settings.value(prefix + "volume", 100).toInt());
}

void AudioSourceRow::SaveSettings(QSettings& settings, const QString& prefix) const {
    settings.setValue(prefix + "enabled", enabled_box_->isChecked());
    settings.setValue(prefix + "device", device_combo_->currentData().toString());
    settings.setValue(prefix + "volume", volume_slider_->value());
}

void AudioSourceRow::SetLocked(bool locked) {
    locked_ = locked;
    UpdateEnabled();
}

void AudioSourceRow::UpdateEnabled() {
    const bool has_devices = device_combo_->count() > 0;
    enabled_box_->setEnabled(has_devices && !locked_);
    const bool on = has_devices && enabled_box_->isChecked();
    device_combo_->setEnabled(on && !locked_);
    volume_slider_->setEnabled(on);
    if (!has_devices) {
        // 未找到设备
        enabled_box_->setToolTip(QStringLiteral("\u672A\u627E\u5230\u8BBE\u5907"));
    }
}

void AudioSourceRow::ShowVolume(int percent) { volume_label_->setText(QStringLiteral("%1%").arg(percent)); }
