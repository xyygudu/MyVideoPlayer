#include "shared_ui.h"

#include <algorithm>

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QSvgRenderer>
#include <QWidget>

#include <spdlog/spdlog.h>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace shared_ui {

QIcon LoadSvgIcon(const QString& resource_path, int size, const QColor& color) {
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly)) {
        SPDLOG_ERROR("LoadSvgIcon: cannot open {}", resource_path.toStdString());
        return {};
    }
    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("currentColor"), color.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) {
        SPDLOG_ERROR("LoadSvgIcon: invalid SVG in {}", resource_path.toStdString());
        return {};
    }
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    return QIcon(pixmap);
}

QLabel* MakeSectionHeader(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSize(11);
    label->setFont(font);
    return label;
}

QString FormStyleSheet() {
    return QStringLiteral(R"(
QLineEdit, QComboBox, QSpinBox {
    border: 1px solid %1;
    border-radius: 6px;
    padding: 5px 10px;
    background: #FFFFFF;
    min-height: 26px;
    font-size: 13px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid %2; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: %5; background: %4; }
QSpinBox::up-button, QSpinBox::down-button {
    border: none;
    background: %4;
    border-radius: 4px;
    width: 22px;
}
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: %1; }
QSpinBox::up-arrow { image: url(:/icons/spin_up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow { image: url(:/icons/spin_down.svg); width: 10px; height: 10px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { width: 10px; height: 10px; }
QComboBox QAbstractItemView {
    border: 1px solid %1;
    border-radius: 4px;
    background: #FFFFFF;
    selection-background-color: %3;
    outline: none;
    font-size: 13px;
    padding: 4px;
}
QComboBox QAbstractItemView::item {
    min-height: 24px;
    padding: 4px 8px;
}
QPushButton#PrimaryButton {
    background: %2;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 10px 0px;
    font-weight: 600;
    font-size: 14px;
}
QPushButton#PrimaryButton:hover { background: #3D68E8; }
QPushButton#PrimaryButton:disabled { background: #C7D3F5; color: #F5F6F8; }
QPushButton#SecondaryButton {
    background: #FFFFFF;
    border: 1px solid %1;
    border-radius: 6px;
    padding: 5px 14px;
    font-size: 13px;
}
QPushButton#SecondaryButton:hover { background: %6; }
QPushButton#SecondaryButton:disabled { color: %5; }
QProgressBar {
    border: 1px solid %1;
    border-radius: 6px;
    text-align: center;
    background: %4;
    height: 16px;
}
QProgressBar::chunk { background: %2; border-radius: 5px; }
QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: %2; }
QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background: %1; }
QLabel { font-size: 12px; }
)")
        .arg(ui_theme::kBorder.name(), ui_theme::kAccent.name(), ui_theme::kAccentSoft.name(),
             ui_theme::kBgNav.name(), ui_theme::kTextSecondary.name(), ui_theme::kHoverTint.name());
}

namespace {

// Swallows wheel events of unfocused value widgets without accepting them,
// so QApplication passes them on to the parents (the scroll area).
class WheelGuard : public QObject {
  public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Wheel && !static_cast<QWidget*>(watched)->hasFocus()) {
            event->ignore();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace

void GuardWheel(QWidget* root) {
    auto* guard = new WheelGuard(root);
    const QList<QWidget*> widgets = root->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        if (qobject_cast<QComboBox*>(widget) || qobject_cast<QSlider*>(widget) ||
            qobject_cast<QAbstractSpinBox*>(widget)) {
            widget->setFocusPolicy(Qt::StrongFocus);
            widget->installEventFilter(guard);
        }
    }
}

QString FormatDuration(double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QChar('0'))
        .arg(total / 60 % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

QString FormatBytes(qint64 bytes) {
    constexpr double kMiB = 1024.0 * 1024.0;
    if (bytes >= 1024 * kMiB) {
        return QStringLiteral("%1 GB").arg(bytes / (1024 * kMiB), 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(bytes / kMiB, 0, 'f', 1);
}

void ExcludeFromCapture(QWidget* window) {
#ifdef Q_OS_WIN
    // WDA_EXCLUDEFROMCAPTURE; older Windows rejects it and the window stays capturable.
    constexpr DWORD kExcludeFromCapture = 0x00000011;
    auto* hwnd = reinterpret_cast<HWND>(window->winId());
    if (!SetWindowDisplayAffinity(hwnd, kExcludeFromCapture)) {
        SPDLOG_WARN("ExcludeFromCapture: not supported here ({}), the overlay may be recorded", GetLastError());
    }
#else
    (void)window;
#endif
}

}  // namespace shared_ui
