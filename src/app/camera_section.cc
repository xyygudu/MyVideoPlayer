#include "camera_section.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>

#include <spdlog/spdlog.h>

#include "mvp/camera_preview.h"

namespace {

constexpr int kPreviewWidth = 240;
constexpr int kPreviewHeight = 135;
// Items of size_combo_ (小 / 中 / 大) and corner_combo_, in order.
constexpr double kWidthRatios[] = {0.18, 0.25, 0.33};
constexpr mvp::OverlayCorner kCorners[] = {mvp::OverlayCorner::kBottomRight, mvp::OverlayCorner::kBottomLeft,
                                           mvp::OverlayCorner::kTopRight, mvp::OverlayCorner::kTopLeft};

}  // namespace

CameraSection::CameraSection(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);
    row->addWidget(BuildControls(), 1);
    preview_label_ = new QLabel(this);
    preview_label_->setFixedSize(kPreviewWidth, kPreviewHeight);
    preview_label_->setAlignment(Qt::AlignCenter);
    preview_label_->setStyleSheet(QStringLiteral("background: #1F2329; color: #9AA0AA; border-radius: 6px;"));
    row->addWidget(preview_label_, 0, Qt::AlignTop);
    RefreshDevices();
}

CameraSection::~CameraSection() {
    preview_.reset();  // Joins the thread that posts frames to this object
}

QWidget* CameraSection::BuildControls() {
    auto* controls = new QWidget(this);
    auto* form = new QFormLayout(controls);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(8);
    enabled_box_ = new QCheckBox(QStringLiteral("\u53E0\u52A0\u6444\u50CF\u5934\u753B\u9762"), controls);  // 叠加摄像头画面
    form->addRow(enabled_box_);
    device_combo_ = new QComboBox(controls);
    form->addRow(QStringLiteral("\u6444\u50CF\u5934\uFF1A"), device_combo_);  // 摄像头：
    corner_combo_ = new QComboBox(controls);
    // 右下角 / 左下角 / 右上角 / 左上角
    corner_combo_->addItems({QStringLiteral("\u53F3\u4E0B\u89D2"), QStringLiteral("\u5DE6\u4E0B\u89D2"),
                             QStringLiteral("\u53F3\u4E0A\u89D2"), QStringLiteral("\u5DE6\u4E0A\u89D2")});
    form->addRow(QStringLiteral("\u4F4D\u7F6E\uFF1A"), corner_combo_);  // 位置：
    size_combo_ = new QComboBox(controls);
    // 小 / 中 / 大
    size_combo_->addItems({QStringLiteral("\u5C0F"), QStringLiteral("\u4E2D"), QStringLiteral("\u5927")});
    size_combo_->setCurrentIndex(1);
    form->addRow(QStringLiteral("\u5927\u5C0F\uFF1A"), size_combo_);  // 大小：
    mirror_box_ = new QCheckBox(QStringLiteral("\u955C\u50CF\u753B\u9762"), controls);  // 镜像画面
    mirror_box_->setChecked(true);
    form->addRow(mirror_box_);
    connect(enabled_box_, &QCheckBox::toggled, this, &CameraSection::UpdatePreview);
    connect(device_combo_, &QComboBox::currentIndexChanged, this, &CameraSection::UpdatePreview);
    return controls;
}

mvp::CameraOverlayOptions CameraSection::Options() const {
    mvp::CameraOverlayOptions options;
    options.enabled = enabled_box_->isChecked() && device_combo_->count() > 0;
    options.device_id = device_combo_->currentData().toString().toStdString();
    options.corner = kCorners[std::clamp(corner_combo_->currentIndex(), 0, 3)];
    options.width_ratio = kWidthRatios[std::clamp(size_combo_->currentIndex(), 0, 2)];
    options.mirror = mirror_box_->isChecked();
    return options;
}

void CameraSection::SetPreviewAllowed(bool allowed) {
    preview_allowed_ = allowed;
    UpdatePreview();
}

void CameraSection::RefreshDevices() {
    const QString selected = device_combo_->currentData().toString();
    {
        const QSignalBlocker blocker(device_combo_);
        device_combo_->clear();
        for (const mvp::CameraInfo& camera : mvp::CaptureDevices::Cameras()) {
            device_combo_->addItem(QString::fromStdString(camera.name), QString::fromStdString(camera.id));
        }
        const int index = device_combo_->findData(selected);
        device_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    const bool has_cameras = device_combo_->count() > 0;
    enabled_box_->setEnabled(has_cameras);
    // 未找到摄像头
    enabled_box_->setToolTip(has_cameras ? QString() : QStringLiteral("\u672A\u627E\u5230\u6444\u50CF\u5934"));
    UpdatePreview();
}

void CameraSection::UpdatePreview() {
    const bool on = enabled_box_->isChecked() && device_combo_->count() > 0;
    for (QWidget* control : {static_cast<QWidget*>(device_combo_), static_cast<QWidget*>(corner_combo_),
                             static_cast<QWidget*>(size_combo_), static_cast<QWidget*>(mirror_box_)}) {
        control->setEnabled(on);
    }
    const std::string id = device_combo_->currentData().toString().toStdString();
    if (on && preview_allowed_ && preview_ && previewing_id_ == id) {
        return;  // Already previewing this camera
    }
    preview_.reset();
    preview_label_->setPixmap(QPixmap());
    if (!on || !preview_allowed_) {
        // 摄像头未开启 / 预览已暂停
        preview_label_->setText(on ? QStringLiteral("\u9884\u89C8\u5DF2\u6682\u505C")
                                   : QStringLiteral("\u6444\u50CF\u5934\u672A\u5F00\u542F"));
        return;
    }
    preview_ = std::make_unique<mvp::CameraPreview>();
    previewing_id_ = id;
    const bool started = preview_->Start(id, kPreviewWidth * 2, [this](const mvp::CapturedFrame& frame) {
        if (frame_pending_.exchange(true)) {
            return;  // The GUI has not shown the previous frame yet
        }
        const QImage image(frame.rgba.data(), frame.width, frame.height, frame.width * 4, QImage::Format_RGBA8888);
        QMetaObject::invokeMethod(this, [this, copy = image.copy()] { ShowFrame(copy); }, Qt::QueuedConnection);
    });
    if (!started) {
        preview_.reset();
        preview_label_->setText(QStringLiteral("\u65E0\u6CD5\u6253\u5F00\u6444\u50CF\u5934"));  // 无法打开摄像头
    }
}

void CameraSection::ShowFrame(const QImage& image) {
    frame_pending_ = false;
    if (!preview_) {
        return;  // Stopped while the frame was queued
    }
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap = QPixmap::fromImage(mirror_box_->isChecked() ? image.mirrored(true, false) : image)
                         .scaled(preview_label_->size() * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(dpr);
    preview_label_->setPixmap(pixmap);
}

void CameraSection::LoadSettings(const QSettings& settings) {
    const int device = device_combo_->findData(settings.value("recorder/camera/device").toString());
    if (device >= 0) {
        device_combo_->setCurrentIndex(device);
    }
    corner_combo_->setCurrentIndex(std::clamp(settings.value("recorder/camera/corner", 0).toInt(), 0, 3));
    size_combo_->setCurrentIndex(std::clamp(settings.value("recorder/camera/size", 1).toInt(), 0, 2));
    mirror_box_->setChecked(settings.value("recorder/camera/mirror", true).toBool());
    enabled_box_->setChecked(settings.value("recorder/camera/enabled", false).toBool());
}

void CameraSection::SaveSettings(QSettings& settings) const {
    settings.setValue("recorder/camera/enabled", enabled_box_->isChecked());
    settings.setValue("recorder/camera/device", device_combo_->currentData().toString());
    settings.setValue("recorder/camera/corner", corner_combo_->currentIndex());
    settings.setValue("recorder/camera/size", size_combo_->currentIndex());
    settings.setValue("recorder/camera/mirror", mirror_box_->isChecked());
}
