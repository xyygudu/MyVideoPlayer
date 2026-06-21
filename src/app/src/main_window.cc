#include "main_window.h"

#include <cmath>

#include <QFileDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "video_widget.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), slider_pressed_(false) {
    player_ = std::make_unique<mvp::MediaPlayer>();
    SetupUi();

    // Wire native window handle to player for SDL3 rendering
    player_->SetWindowHandle(reinterpret_cast<void*>(video_widget_->winId()));

    // Forward resize events to player/renderer
    video_widget_->SetResizeCallback([this](int w, int h) {
        player_->NotifyWindowResized(w, h);
    });

    // Timer for updating progress
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::OnTimerTick);
    timer_->start(100);  // 100ms interval
}

MainWindow::~MainWindow() {
    timer_->stop();
    player_->Close();
}

void MainWindow::SetupUi() {
    setWindowTitle("MyVideoPlayer");
    resize(800, 600);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* main_layout = new QVBoxLayout(central);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // Video area
    video_widget_ = new VideoWidget(central);
    main_layout->addWidget(video_widget_, 1);

    // Control bar
    auto* control_widget = new QWidget(central);
    auto* control_layout = new QHBoxLayout(control_widget);
    control_layout->setContentsMargins(8, 4, 8, 4);

    open_btn_ = new QPushButton(QStringLiteral("\u6253\u5F00"), control_widget);  // "打开"
    connect(open_btn_, &QPushButton::clicked, this, &MainWindow::OnOpenFile);
    control_layout->addWidget(open_btn_);

    play_pause_btn_ = new QPushButton(QStringLiteral("\u25B6"), control_widget);  // "▶"
    play_pause_btn_->setFixedWidth(40);
    connect(play_pause_btn_, &QPushButton::clicked, this, &MainWindow::OnPlayPause);
    control_layout->addWidget(play_pause_btn_);

    progress_slider_ = new QSlider(Qt::Horizontal, control_widget);
    progress_slider_->setRange(0, 1000);
    progress_slider_->installEventFilter(this);
    connect(progress_slider_, &QSlider::sliderPressed, this, [this] { slider_pressed_ = true; });
    connect(progress_slider_, &QSlider::sliderReleased, this, [this] {
        OnSliderMoved(progress_slider_->value());
        // slider_pressed_ stays true — timer clears it when video catches up
    });
    connect(progress_slider_, &QSlider::sliderMoved, this, &MainWindow::OnSliderMoved);
    control_layout->addWidget(progress_slider_, 1);

    time_label_ = new QLabel("00:00:00.00 / 00:00:00", control_widget);
    time_label_->setFixedWidth(180);
    control_layout->addWidget(time_label_);

    main_layout->addWidget(control_widget);
}

void MainWindow::OnOpenFile() {
    QString filepath = QFileDialog::getOpenFileName(
        this, QStringLiteral("\u6253\u5F00\u89C6\u9891\u6587\u4EF6"), QString(),
        "Video Files (*.mp4 *.avi *.mkv *.mov *.flv *.wmv);;All Files (*)");
    if (filepath.isEmpty()) return;

    SPDLOG_INFO("UI: open file '{}'", filepath.toStdString());
    player_->Close();
    if (player_->Open(filepath.toStdString())) {
        player_->Play();
        play_pause_btn_->setText(QStringLiteral("\u23F8"));  // "⏸"
    }
}

void MainWindow::OnPlayPause() {
    if (player_->IsPlaying()) {
        SPDLOG_INFO("UI: pause");
        player_->Pause();
        play_pause_btn_->setText(QStringLiteral("\u25B6"));  // "▶"
    } else {
        SPDLOG_INFO("UI: play");
        player_->Play();
        play_pause_btn_->setText(QStringLiteral("\u23F8"));  // "⏸"
    }
}

void MainWindow::OnSliderMoved(int value) {
    double duration = player_->Duration();
    if (duration <= 0) return;
    double pos = static_cast<double>(value) / 1000.0 * duration;
    player_->Seek(pos);
}

void MainWindow::OnTimerTick() {
    double duration = player_->Duration();
    double position = player_->CurrentPosition();
    double video_pos = position;  // MediaPlayer uses unified clock
    double fps = player_->VideoFps();
    auto state = player_->State();

    // When finished, update button and stop advancing
    if (state == mvp::PlaybackState::kFinished) {
        play_pause_btn_->setText(QStringLiteral("\u25B6"));  // "▶"
    }

    // slider_pressed_ stays true after release until video catches up
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

    // Time label: "HH:MM:SS.FF / HH:MM:SS" where FF is frame-in-second
    if (fps > 0 && duration > 0) {
        int frame_in_second = static_cast<int>((video_pos - static_cast<int>(video_pos)) * fps);
        QString pos_str = FormatTime(video_pos) + QString(".%1").arg(frame_in_second, 2, 10, QChar('0'));
        time_label_->setText(pos_str + " / " + FormatTime(duration));
    } else {
        time_label_->setText(FormatTime(position) + " / " + FormatTime(duration));
    }
}

QString MainWindow::FormatTime(double seconds) const {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == progress_slider_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton) {
                int value = QStyle::sliderValueFromPosition(
                    progress_slider_->minimum(), progress_slider_->maximum(),
                    mouse_event->pos().x(), progress_slider_->width());
                progress_slider_->setValue(value);
                slider_pressed_ = true;
                // Don't seek on press — wait for drag or release
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && slider_pressed_) {
            auto* mouse_event = static_cast<QMouseEvent*>(event);
            int value = QStyle::sliderValueFromPosition(
                progress_slider_->minimum(), progress_slider_->maximum(),
                mouse_event->pos().x(), progress_slider_->width());
            progress_slider_->setValue(value);
            OnSliderMoved(value);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && slider_pressed_) {
            auto* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton) {
                int value = QStyle::sliderValueFromPosition(
                    progress_slider_->minimum(), progress_slider_->maximum(),
                    mouse_event->pos().x(), progress_slider_->width());
                progress_slider_->setValue(value);
                OnSliderMoved(value);
                // slider_pressed_ stays true — timer clears it when video catches up
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
