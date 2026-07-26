#include "player_page.h"

#include <cmath>

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "effect_panel.h"
#include "icon_button.h"
#include "app_theme.h"
#include "video_widget.h"

namespace {

QIcon LoadButtonIcon(const QString& resource_path, int size) {
    SPDLOG_DEBUG("LoadButtonIcon: loading {} at {}px", resource_path.toStdString(), size);
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly)) {
        SPDLOG_ERROR("LoadButtonIcon: FAILED to open {}", resource_path.toStdString());
        return {};
    }
    qint64 bytes = file.size();
    QString svg = QString::fromUtf8(file.readAll());
    SPDLOG_DEBUG("LoadButtonIcon: read {} bytes from {}", bytes, resource_path.toStdString());
    svg.replace(QStringLiteral("currentColor"), ui_theme::kTextPrimary.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) {
        SPDLOG_ERROR("LoadButtonIcon: QSvgRenderer invalid for {}", resource_path.toStdString());
        return {};
    }
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        renderer.render(&p, QRectF(0, 0, size, size));
    }
    SPDLOG_DEBUG("LoadButtonIcon: success for {}", resource_path.toStdString());
    return QIcon(pixmap);
}

}  // namespace

namespace {

const char* kSliderStyle = R"(
QSlider::groove:horizontal { height: 4px; background: #E5E6EB; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #4F7CFF; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #4F7CFF;
}
)";

}  // namespace

PlayerPage::PlayerPage(QWidget* parent) : QWidget(parent) {
    player_ = std::make_unique<mvp::MediaPlayer>();
    SetupUi();

    player_->SetWindowHandle(reinterpret_cast<void*>(video_widget_->winId()));
    video_widget_->SetResizeCallback(
        [this](int w, int h) { player_->NotifyWindowResized(w, h); });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &PlayerPage::OnTimerTick);
    timer_->start(100);
}

PlayerPage::~PlayerPage() {
    timer_->stop();
    video_widget_->SetVideoPlaying(false);
    player_->Close();
}

void PlayerPage::SetupUi() {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->addWidget(splitter);

    auto* left = new QWidget(splitter);
    auto* main_layout = new QVBoxLayout(left);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    video_widget_ = new VideoWidget(left);
    main_layout->addWidget(video_widget_, 1);
    main_layout->addWidget(BuildControlBar());

    effect_panel_ = new EffectPanel(splitter);

    splitter->addWidget(left);
    splitter->addWidget(effect_panel_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({740, 260});
}

QWidget* PlayerPage::BuildControlBar() {
    auto* control_widget = new QWidget(this);
    auto* control_layout = new QHBoxLayout(control_widget);
    control_layout->setContentsMargins(8, 4, 8, 4);

    open_btn_ = new IconButton(IconButton::IconKind::kOpenFolder, control_widget);
    open_btn_->setFixedSize(32, 32);
    open_btn_->SetCustomIcon(LoadButtonIcon(QStringLiteral(":/icons/file_open.svg"), 20));
    connect(open_btn_, &QAbstractButton::clicked, this, &PlayerPage::OnOpenFile);
    control_layout->addWidget(open_btn_);

    play_pause_btn_ = new IconButton(IconButton::IconKind::kPlay, control_widget);
    play_pause_btn_->setFixedSize(32, 32);
    play_pause_btn_->SetCustomIcon(LoadButtonIcon(QStringLiteral(":/icons/play.svg"), 20));
    connect(play_pause_btn_, &QAbstractButton::clicked, this, &PlayerPage::OnPlayPause);
    control_layout->addWidget(play_pause_btn_);

    progress_slider_ = new QSlider(Qt::Horizontal, control_widget);
    progress_slider_->setRange(0, 1000);
    progress_slider_->setStyleSheet(kSliderStyle);
    progress_slider_->installEventFilter(this);
    connect(progress_slider_, &QSlider::sliderPressed, this,
            [this] { slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this,
            [this] { OnSliderMoved(progress_slider_->value()); });
    connect(progress_slider_, &QSlider::sliderMoved, this, &PlayerPage::OnSliderMoved);
    control_layout->addWidget(progress_slider_, 1);

    time_label_ = new QLabel("00:00:00.00 / 00:00:00", control_widget);
    time_label_->setFixedWidth(180);
    control_layout->addWidget(time_label_);

    return control_widget;
}

void PlayerPage::OnOpenFile() {
    QString filepath = QFileDialog::getOpenFileName(
        this, QStringLiteral("\u6253\u5F00\u89C6\u9891\u6587\u4EF6"), QString(),
        "Video Files (*.mp4 *.avi *.mkv *.mov *.flv *.wmv);;All Files (*)");
    if (filepath.isEmpty()) return;

    SPDLOG_INFO("UI: open file '{}'", filepath.toStdString());
    player_->Close();
    video_widget_->SetVideoPlaying(false);
    if (player_->Open(filepath.toStdString())) {
        player_->Play();
        video_widget_->SetVideoPlaying(true);
        play_pause_btn_->SetCustomIcon(
            LoadButtonIcon(QStringLiteral(":/icons/pause.svg"), 20));
        play_pause_btn_->SetIconKind(IconButton::IconKind::kPause);
    }
    effect_panel_->RefreshFromPlayer(player_.get());
}

void PlayerPage::OnPlayPause() {
    if (player_->IsPlaying()) {
        SPDLOG_INFO("UI: pause");
        player_->Pause();
        play_pause_btn_->SetCustomIcon(
            LoadButtonIcon(QStringLiteral(":/icons/play.svg"), 20));
        play_pause_btn_->SetIconKind(IconButton::IconKind::kPlay);
    } else {
        SPDLOG_INFO("UI: play");
        player_->Play();
        play_pause_btn_->SetCustomIcon(
            LoadButtonIcon(QStringLiteral(":/icons/pause.svg"), 20));
        play_pause_btn_->SetIconKind(IconButton::IconKind::kPause);
    }
}

void PlayerPage::OnSliderMoved(int value) {
    double duration = player_->Duration();
    if (duration <= 0) return;
    double pos = static_cast<double>(value) / 1000.0 * duration;
    player_->Seek(pos);
}

void PlayerPage::OnTimerTick() {
    double duration = player_->Duration();
    double video_pos = player_->CurrentPosition();
    double fps = player_->VideoFps();
    auto state = player_->State();

    if (state == mvp::PlaybackState::kFinished) {
        play_pause_btn_->SetCustomIcon(
            LoadButtonIcon(QStringLiteral(":/icons/play.svg"), 20));
        play_pause_btn_->SetIconKind(IconButton::IconKind::kPlay);
    }

    if (slider_pressed_ && !progress_slider_->isSliderDown() && duration > 0) {
        double slider_seconds = progress_slider_->value() / 1000.0 * duration;
        if (std::abs(video_pos - slider_seconds) < 1.0) {
            slider_pressed_ = false;
        }
    }
    if (!slider_pressed_ && duration > 0) {
        int slider_pos = static_cast<int>(video_pos / duration * 1000.0);
        progress_slider_->setValue(slider_pos);
    }

    if (fps > 0 && duration > 0) {
        int frame_in_second = static_cast<int>((video_pos - static_cast<int>(video_pos)) * fps);
        QString pos_str =
            FormatTime(video_pos) + QString(".%1").arg(frame_in_second, 2, 10, QChar('0'));
        time_label_->setText(pos_str + " / " + FormatTime(duration));
    } else {
        time_label_->setText(FormatTime(video_pos) + " / " + FormatTime(duration));
    }
}

QString PlayerPage::FormatTime(double seconds) const {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

bool PlayerPage::eventFilter(QObject* obj, QEvent* event) {
    if (obj != progress_slider_) {
        return QWidget::eventFilter(obj, event);
    }

    auto slider_value_at = [this](const QPoint& pos) {
        return QStyle::sliderValueFromPosition(progress_slider_->minimum(),
                                               progress_slider_->maximum(), pos.x(),
                                               progress_slider_->width());
    };

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
            progress_slider_->setValue(slider_value_at(mouse_event->pos()));
            slider_pressed_ = true;
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && slider_pressed_) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        int value = slider_value_at(mouse_event->pos());
        progress_slider_->setValue(value);
        OnSliderMoved(value);
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease && slider_pressed_) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
            int value = slider_value_at(mouse_event->pos());
            progress_slider_->setValue(value);
            OnSliderMoved(value);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
