#include "recorder_page.h"

#include <algorithm>
#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "app_theme.h"
#include "audio_source_row.h"
#include "camera_section.h"
#include "capture_area_section.h"
#include "global_hotkeys.h"
#include "icon_button.h"
#include "shared_ui.h"

namespace {

using Phase = RecordingController::Phase;

constexpr int kStartStopHotkeyId = 1;
constexpr int kPauseHotkeyId = 2;
constexpr int kStartStopKey = 9;  // Ctrl+Shift+F9 (Alt+F9 and Ctrl+Shift+R are often taken)
constexpr int kPauseKey = 10;     // Ctrl+Shift+F10
constexpr int kCountdownSeconds = 3;
constexpr int kMaxRecent = 10;
constexpr int64_t kAudioBitrate = 160000;
constexpr int kFrameRates[] = {15, 24, 30, 60};
constexpr int kQualityCrf[] = {18, 23, 28};  // 高 / 标准 / 省空间
constexpr mvp::ScreenCaptureMethod kMethods[] = {mvp::ScreenCaptureMethod::kAuto,
                                                 mvp::ScreenCaptureMethod::kDesktopDuplication,
                                                 mvp::ScreenCaptureMethod::kGdi};

QString EncoderLabel(const std::string& codec) {
    if (codec == "h264_nvenc") return QStringLiteral("NVIDIA NVENC\uFF08\u786C\u4EF6\uFF09");    // （硬件）
    if (codec == "h264_qsv") return QStringLiteral("Intel Quick Sync\uFF08\u786C\u4EF6\uFF09");
    if (codec == "h264_amf") return QStringLiteral("AMD AMF\uFF08\u786C\u4EF6\uFF09");
    if (codec == "libx264") return QStringLiteral("x264\uFF08CPU\uFF09");
    return QString::fromStdString(codec);
}

QString DefaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/MyVideoPlayer");
}

QLabel* MakeNote(const QString& text, QWidget* parent) {
    auto* note = new QLabel(text, parent);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(ui_theme::kTextSecondary.name()));
    return note;
}

QWidget* MakeSection(const QString& title, QWidget* parent, QVBoxLayout** body) {
    auto* section = new QWidget(parent);
    *body = new QVBoxLayout(section);
    (*body)->setContentsMargins(0, 0, 0, 0);
    (*body)->setSpacing(10);
    (*body)->addWidget(shared_ui::MakeSectionHeader(title, section));
    return section;
}

QFormLayout* MakeForm() {
    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(12);
    return form;
}

}  // namespace

RecorderPage::RecorderPage(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ui_theme::kBgContent);
    setPalette(pal);
    setStyleSheet(shared_ui::FormStyleSheet());

    controller_ = new RecordingController(this);
    QWidget* content = BuildContent();
    content->setAutoFillBackground(true);
    content->setPalette(pal);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    shared_ui::GuardWheel(content);

    ConnectController();
    LoadSettings();
    SetupHotkeys();
    ProbeEncoders();
    RefreshRecent();
    OnPhaseChanged(Phase::kIdle);
}

RecorderPage::~RecorderPage() {
    SaveSettings();
    if (encoder_probe_) {
        encoder_probe_->wait();
    }
}

QWidget* RecorderPage::BuildContent() {
    auto* content = new QWidget(this);
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(ui_theme::kContentPadding, ui_theme::kContentPadding, ui_theme::kContentPadding,
                               ui_theme::kContentPadding);
    column->setSpacing(22);
    column->addWidget(BuildActionSection());

    QVBoxLayout* area_body = nullptr;
    column->addWidget(MakeSection(QStringLiteral("\u5F55\u5236\u533A\u57DF"), content, &area_body));  // 录制区域
    area_section_ = new CaptureAreaSection(content);
    area_body->addWidget(area_section_);

    column->addWidget(BuildVideoSection());
    column->addWidget(BuildAudioSection());

    QVBoxLayout* camera_body = nullptr;
    column->addWidget(MakeSection(QStringLiteral("\u6444\u50CF\u5934"), content, &camera_body));  // 摄像头
    camera_section_ = new CameraSection(content);
    camera_body->addWidget(camera_section_);

    column->addWidget(BuildOutputSection());
    column->addWidget(BuildRecentSection());
    column->addStretch(1);
    return content;
}

QWidget* RecorderPage::BuildActionSection() {
    auto* section = new QWidget(this);
    auto* v = new QVBoxLayout(section);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    auto* row = new QHBoxLayout();
    start_button_ = new QPushButton(section);
    start_button_->setObjectName(QStringLiteral("PrimaryButton"));
    start_button_->setFixedHeight(40);
    connect(start_button_, &QPushButton::clicked, this, &RecorderPage::OnStartStop);
    pause_button_ = new QPushButton(section);
    pause_button_->setObjectName(QStringLiteral("SecondaryButton"));
    pause_button_->setFixedSize(110, 40);
    connect(pause_button_, &QPushButton::clicked, controller_, &RecordingController::TogglePause);
    row->addWidget(start_button_, 1);
    row->addWidget(pause_button_);
    v->addLayout(row);

    status_label_ = new QLabel(section);
    QFont font = status_label_->font();
    font.setPointSize(10);
    status_label_->setFont(font);
    status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(status_label_);
    warning_label_ = new QLabel(section);
    warning_label_->setWordWrap(true);
    warning_label_->setStyleSheet(QStringLiteral("color: #C77700;"));
    warning_label_->hide();
    v->addWidget(warning_label_);
    hotkey_label_ = MakeNote(QString(), section);
    v->addWidget(hotkey_label_);
    return section;
}

QWidget* RecorderPage::BuildVideoSection() {
    QVBoxLayout* body = nullptr;
    video_section_ = MakeSection(QStringLiteral("\u753B\u9762"), this, &body);  // 画面
    QFormLayout* form = MakeForm();
    fps_combo_ = new QComboBox(video_section_);
    for (int fps : kFrameRates) {
        fps_combo_->addItem(QStringLiteral("%1 fps").arg(fps), fps);
    }
    fps_combo_->setCurrentIndex(2);
    form->addRow(QStringLiteral("\u5E27\u7387\uFF1A"), fps_combo_);  // 帧率：
    quality_combo_ = new QComboBox(video_section_);
    // 高（文件较大） / 标准 / 省空间
    quality_combo_->addItems({QStringLiteral("\u9AD8\uFF08\u6587\u4EF6\u8F83\u5927\uFF09"),
                              QStringLiteral("\u6807\u51C6"), QStringLiteral("\u7701\u7A7A\u95F4")});
    quality_combo_->setCurrentIndex(1);
    form->addRow(QStringLiteral("\u753B\u8D28\uFF1A"), quality_combo_);  // 画质：
    encoder_combo_ = new QComboBox(video_section_);
    encoder_combo_->addItem(EncoderLabel("libx264"), QStringLiteral("libx264"));
    form->addRow(QStringLiteral("\u7F16\u7801\u5668\uFF1A"), encoder_combo_);  // 编码器：
    method_combo_ = new QComboBox(video_section_);
    // 自动 / 桌面复制（DXGI） / GDI（兼容性好）
    method_combo_->addItems({QStringLiteral("\u81EA\u52A8"), QStringLiteral("\u684C\u9762\u590D\u5236\uFF08DXGI\uFF09"),
                             QStringLiteral("GDI\uFF08\u517C\u5BB9\u6027\u597D\uFF09")});
    form->addRow(QStringLiteral("\u6355\u83B7\u65B9\u5F0F\uFF1A"), method_combo_);  // 捕获方式：
    cursor_box_ = new QCheckBox(QStringLiteral("\u5F55\u5236\u9F20\u6807\u6307\u9488"), video_section_);  // 录制鼠标指针
    cursor_box_->setChecked(true);
    clicks_box_ = new QCheckBox(QStringLiteral("\u9AD8\u4EAE\u9F20\u6807\u70B9\u51FB"), video_section_);  // 高亮鼠标点击
    connect(cursor_box_, &QCheckBox::toggled, clicks_box_, &QCheckBox::setEnabled);
    auto* mouse_row = new QHBoxLayout();
    mouse_row->addWidget(cursor_box_);
    mouse_row->addWidget(clicks_box_);
    mouse_row->addStretch(1);
    form->addRow(QStringLiteral("\u9F20\u6807\uFF1A"), mouse_row);  // 鼠标：
    body->addLayout(form);
    return video_section_;
}

QWidget* RecorderPage::BuildAudioSection() {
    QVBoxLayout* body = nullptr;
    QWidget* section = MakeSection(QStringLiteral("\u58F0\u97F3"), this, &body);  // 声音
    system_row_ = new AudioSourceRow(mvp::AudioDeviceKind::kOutput, QStringLiteral("\u7CFB\u7EDF\u58F0\u97F3"),
                                     true, section);  // 系统声音
    mic_row_ = new AudioSourceRow(mvp::AudioDeviceKind::kInput, QStringLiteral("\u9EA6\u514B\u98CE"), false,
                                  section);  // 麦克风
    connect(system_row_, &AudioSourceRow::VolumeChanged, this,
            [this](float volume) { controller_->SetVolume(mvp::AudioSourceKind::kSystemAudio, volume); });
    connect(mic_row_, &AudioSourceRow::VolumeChanged, this,
            [this](float volume) { controller_->SetVolume(mvp::AudioSourceKind::kMicrophone, volume); });
    body->addWidget(system_row_);
    body->addWidget(mic_row_);
    // 系统声音即耳机或扬声器正在播放的声音；音量在录制中也可调整。
    body->addWidget(MakeNote(QStringLiteral("\u7CFB\u7EDF\u58F0\u97F3\u5373\u8033\u673A\u6216\u626C\u58F0\u5668"
                                            "\u6B63\u5728\u64AD\u653E\u7684\u58F0\u97F3\uFF1B\u97F3\u91CF\u5728"
                                            "\u5F55\u5236\u4E2D\u4E5F\u53EF\u8C03\u6574\u3002"),
                             section));
    return section;
}

QWidget* RecorderPage::BuildOutputSection() {
    QVBoxLayout* body = nullptr;
    output_section_ = MakeSection(QStringLiteral("\u8F93\u51FA"), this, &body);  // 输出
    QFormLayout* form = MakeForm();
    auto* folder_row = new QHBoxLayout();
    folder_edit_ = new QLineEdit(output_section_);
    folder_edit_->setReadOnly(true);
    auto* browse = new IconButton(IconButton::IconKind::kOpenFolder, output_section_);
    browse->setFixedSize(30, 30);
    browse->SetCustomIcon(shared_ui::LoadSvgIcon(QStringLiteral(":/icons/file_open.svg"), 18));
    connect(browse, &QAbstractButton::clicked, this, [this] {
        // 选择保存位置
        const QString folder = QFileDialog::getExistingDirectory(
            this, QStringLiteral("\u9009\u62E9\u4FDD\u5B58\u4F4D\u7F6E"), folder_edit_->text());
        if (!folder.isEmpty()) folder_edit_->setText(QDir::toNativeSeparators(folder));
    });
    folder_row->addWidget(folder_edit_, 1);
    folder_row->addWidget(browse);
    form->addRow(QStringLiteral("\u4FDD\u5B58\u5230\uFF1A"), folder_row);  // 保存到：
    container_combo_ = new QComboBox(output_section_);
    container_combo_->addItem(QStringLiteral("MP4\uFF08\u901A\u7528\uFF09"), QStringLiteral("mp4"));  // （通用）
    // MKV（中断也能保留已录内容）
    container_combo_->addItem(QStringLiteral("MKV\uFF08\u4E2D\u65AD\u4E5F\u80FD\u4FDD\u7559"
                                             "\u5DF2\u5F55\u5185\u5BB9\uFF09"),
                              QStringLiteral("mkv"));
    form->addRow(QStringLiteral("\u683C\u5F0F\uFF1A"), container_combo_);  // 格式：
    countdown_box_ = new QCheckBox(QStringLiteral("\u5F00\u59CB\u524D\u5012\u8BA1\u65F6 3 \u79D2"),
                                   output_section_);  // 开始前倒计时 3 秒
    countdown_box_->setChecked(true);
    minimize_box_ = new QCheckBox(QStringLiteral("\u5F55\u5236\u65F6\u6700\u5C0F\u5316\u672C\u7A97\u53E3"),
                                  output_section_);  // 录制时最小化本窗口
    minimize_box_->setChecked(true);
    form->addRow(countdown_box_);
    form->addRow(minimize_box_);
    body->addLayout(form);
    return output_section_;
}

QWidget* RecorderPage::BuildRecentSection() {
    QVBoxLayout* body = nullptr;
    QWidget* section = MakeSection(QStringLiteral("\u6700\u8FD1\u5F55\u5236"), this, &body);  // 最近录制
    recent_list_ = new QListWidget(section);
    recent_list_->setFixedHeight(150);
    recent_list_->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid %1; border-radius: 6px; }")
                                    .arg(ui_theme::kBorder.name()));
    connect(recent_list_, &QListWidget::itemDoubleClicked, this, [this] {
        if (!SelectedRecent().isEmpty()) emit PlayRequested(SelectedRecent());
    });
    body->addWidget(recent_list_);
    auto* buttons = new QHBoxLayout();
    auto* play = new QPushButton(QStringLiteral("\u64AD\u653E"), section);  // 播放
    play->setObjectName(QStringLiteral("SecondaryButton"));
    connect(play, &QPushButton::clicked, this, [this] {
        if (!SelectedRecent().isEmpty()) emit PlayRequested(SelectedRecent());
    });
    auto* reveal = new QPushButton(QStringLiteral("\u5728\u6587\u4EF6\u5939\u4E2D\u663E\u793A"), section);  // 在文件夹中显示
    reveal->setObjectName(QStringLiteral("SecondaryButton"));
    connect(reveal, &QPushButton::clicked, this, [this] {
        if (SelectedRecent().isEmpty()) return;
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(SelectedRecent())});
    });
    buttons->addWidget(play);
    buttons->addWidget(reveal);
    buttons->addStretch(1);
    body->addLayout(buttons);
    return section;
}

void RecorderPage::ConnectController() {
    connect(controller_, &RecordingController::PhaseChanged, this, &RecorderPage::OnPhaseChanged);
    connect(controller_, &RecordingController::StatsUpdated, this, &RecorderPage::OnStats);
    connect(controller_, &RecordingController::Finished, this, &RecorderPage::OnFinished);
    connect(controller_, &RecordingController::StartFailed, this, [this] {
        window()->showNormal();
        // 录制启动失败，请查看日志
        status_label_->setText(
            QStringLiteral("\u5F55\u5236\u542F\u52A8\u5931\u8D25\uFF0C\u8BF7\u67E5\u770B\u65E5\u5FD7"));
    });
}

void RecorderPage::SetupHotkeys() {
    hotkeys_ = new GlobalHotkeys(this);
    const bool start_ok = hotkeys_->RegisterCtrlShiftF(kStartStopHotkeyId, kStartStopKey);
    const bool pause_ok = hotkeys_->RegisterCtrlShiftF(kPauseHotkeyId, kPauseKey);
    connect(hotkeys_, &GlobalHotkeys::Activated, this, [this](int id) {
        if (id == kStartStopHotkeyId) {
            OnStartStop();
        } else if (id == kPauseHotkeyId) {
            controller_->TogglePause();
        }
    });
    // 快捷键：%1 开始/停止，%2 暂停/继续
    QString text = QStringLiteral("\u5FEB\u6377\u952E\uFF1A%1 \u5F00\u59CB/\u505C\u6B62\uFF0C"
                                  "%2 \u6682\u505C/\u7EE7\u7EED")
                       .arg(GlobalHotkeys::Text(kStartStopKey), GlobalHotkeys::Text(kPauseKey));
    if (!start_ok || !pause_ok) {
        // （部分被其他程序占用，未生效）
        text += QStringLiteral("\uFF08\u90E8\u5206\u88AB\u5176\u4ED6\u7A0B\u5E8F\u5360\u7528\uFF0C"
                               "\u672A\u751F\u6548\uFF09");
    }
    hotkey_label_->setText(text);
    start_button_->setToolTip(GlobalHotkeys::Text(kStartStopKey));
    pause_button_->setToolTip(GlobalHotkeys::Text(kPauseKey));
}

void RecorderPage::ProbeEncoders() {
    // Opening every hardware encoder takes a moment: off the GUI thread.
    QThread* probe = QThread::create([this] { probed_encoders_ = mvp::CaptureDevices::VideoEncoders(); });
    encoder_probe_ = probe;
    connect(probe, &QThread::finished, this, &RecorderPage::PopulateEncoders);
    connect(probe, &QThread::finished, probe, &QObject::deleteLater);
    probe->start(QThread::LowPriority);
}

void RecorderPage::PopulateEncoders() {
    // Without a saved choice the first, i.e. a hardware encoder when there is one.
    const QString saved = QSettings().value("recorder/video/encoder").toString();
    const QSignalBlocker blocker(encoder_combo_);
    encoder_combo_->clear();
    for (const std::string& codec : probed_encoders_) {
        encoder_combo_->addItem(EncoderLabel(codec), QString::fromStdString(codec));
    }
    if (encoder_combo_->count() == 0) {
        encoder_combo_->addItem(EncoderLabel("libx264"), QStringLiteral("libx264"));
    }
    const int index = encoder_combo_->findData(saved);
    encoder_combo_->setCurrentIndex(index >= 0 ? index : 0);
}

void RecorderPage::OnStartStop() {
    switch (controller_->phase()) {
        case Phase::kIdle:
            StartRecording();
            break;
        case Phase::kCountdown:
        case Phase::kRecording:
        case Phase::kPaused:
            controller_->Stop();
            break;
        case Phase::kStopping:
            break;
    }
}

void RecorderPage::StartRecording() {
    mvp::RecordOptions options;
    QString problem;
    if (!BuildOptions(&options, &problem)) {
        SPDLOG_INFO("UI: cannot record yet: {}", problem.toStdString());
        status_label_->setText(problem);
        return;
    }
    SPDLOG_INFO("UI: start recording to '{}'", options.output_path);
    SaveSettings();
    warning_label_->hide();
    camera_section_->SetPreviewAllowed(false);  // The recording needs the camera
    requested_ = options;

    RecordingController::Presentation presentation;
    presentation.countdown_seconds = countdown_box_->isChecked() ? kCountdownSeconds : 0;
    presentation.area = area_section_->Area();
    presentation.outline = area_section_->Mode() != mvp::CaptureMode::kMonitor;
    presentation.follow_window = area_section_->Mode() == mvp::CaptureMode::kWindow ? area_section_->WindowId() : 0;
    if (minimize_box_->isChecked()) {
        window()->showMinimized();
    }
    controller_->Start(options, presentation);
}

bool RecorderPage::BuildOptions(mvp::RecordOptions* options, QString* problem) const {
    if (!area_section_->Fill(&options->video, problem)) {
        return false;
    }
    options->video.frame_rate = fps_combo_->currentData().toInt();
    options->video.capture_cursor = cursor_box_->isChecked();
    options->video.highlight_clicks = cursor_box_->isChecked() && clicks_box_->isChecked();
    options->video.method = kMethods[std::clamp(method_combo_->currentIndex(), 0, 2)];
    options->system_audio = system_row_->Options();
    options->microphone = mic_row_->Options();
    options->camera = camera_section_->Options();
    options->video_encoder.codec_name = encoder_combo_->currentData().toString().toStdString();
    options->video_encoder.crf = kQualityCrf[std::clamp(quality_combo_->currentIndex(), 0, 2)];
    options->audio_encoder.codec_name = "aac";
    options->audio_encoder.rate_control = mvp::RateControlMode::kBitrate;
    options->audio_encoder.bitrate_bps = kAudioBitrate;
    if (!QDir().mkpath(folder_edit_->text())) {
        // 无法创建输出文件夹
        *problem = QStringLiteral("\u65E0\u6CD5\u521B\u5EFA\u8F93\u51FA\u6587\u4EF6\u5939");
        return false;
    }
    options->output_path = NextOutputPath().toStdString();
    return true;
}

QString RecorderPage::NextOutputPath() const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString name = QStringLiteral("\u5F55\u5C4F_%1.%2").arg(stamp, container_combo_->currentData().toString());
    return QDir(folder_edit_->text()).filePath(name);  // 录屏_<time>.<ext>
}

void RecorderPage::OnPhaseChanged(Phase phase) {
    const bool idle = phase == Phase::kIdle;
    // 开始录制 / 取消 / 停止录制
    start_button_->setText(idle ? QStringLiteral("\u5F00\u59CB\u5F55\u5236")
                                : phase == Phase::kCountdown ? QStringLiteral("\u53D6\u6D88")
                                                             : QStringLiteral("\u505C\u6B62\u5F55\u5236"));
    start_button_->setEnabled(phase != Phase::kStopping);
    pause_button_->setEnabled(phase == Phase::kRecording || phase == Phase::kPaused);
    // 继续 / 暂停
    pause_button_->setText(phase == Phase::kPaused ? QStringLiteral("\u7EE7\u7EED") : QStringLiteral("\u6682\u505C"));
    for (QWidget* locked : {static_cast<QWidget*>(area_section_), video_section_,
                            static_cast<QWidget*>(camera_section_), output_section_}) {
        locked->setEnabled(idle);
    }
    system_row_->SetLocked(!idle);
    mic_row_->SetLocked(!idle);
    if (phase == Phase::kCountdown) {
        status_label_->setText(QStringLiteral("\u5373\u5C06\u5F00\u59CB\u2026"));  // 即将开始…
    } else if (phase == Phase::kStopping) {
        status_label_->setText(QStringLiteral("\u6B63\u5728\u4FDD\u5B58\u2026"));  // 正在保存…
    } else if (phase == Phase::kRecording && (last_phase_ == Phase::kIdle || last_phase_ == Phase::kCountdown)) {
        ReportMissingSources();  // Just started
    } else if (idle && status_label_->text().isEmpty()) {
        status_label_->setText(QStringLiteral("\u5C31\u7EEA"));  // 就绪
    }
    last_phase_ = phase;
}

void RecorderPage::OnStats(const mvp::RecordingStats& stats) {
    system_row_->SetLevel(stats.system_audio_level);
    mic_row_->SetLevel(stats.microphone_level);
    // 已暂停 / 录制中
    QString text = controller_->phase() == Phase::kPaused ? QStringLiteral("\u5DF2\u6682\u505C")
                                                          : QStringLiteral("\u5F55\u5236\u4E2D");
    text += QStringLiteral("  %1  \u00B7  %2").arg(shared_ui::FormatDuration(stats.elapsed_seconds),
                                                   shared_ui::FormatBytes(stats.bytes_written));
    if (stats.frames_dropped > 0) {
        text += QStringLiteral("  \u00B7  \u4E22\u5E27 %1").arg(stats.frames_dropped);  // 丢帧
    }
    status_label_->setText(text);
}

void RecorderPage::OnFinished(const QString& path, bool succeeded) {
    system_row_->ResetLevel();
    mic_row_->ResetLevel();
    window()->showNormal();
    window()->raise();
    window()->activateWindow();
    const QFileInfo file(path);
    const QString native = QDir::toNativeSeparators(path);
    if (succeeded) {
        status_label_->setText(QStringLiteral("\u5DF2\u4FDD\u5B58\uFF1A%1").arg(native));  // 已保存：
    } else if (file.exists() && file.size() > 0) {
        // 录制中断，已保存部分内容：
        status_label_->setText(QStringLiteral("\u5F55\u5236\u4E2D\u65AD\uFF0C\u5DF2\u4FDD\u5B58\u90E8\u5206\u5185\u5BB9"
                                              "\uFF1A%1").arg(native));
    } else {
        // 录制失败，请查看日志
        status_label_->setText(QStringLiteral("\u5F55\u5236\u5931\u8D25\uFF0C\u8BF7\u67E5\u770B\u65E5\u5FD7"));
    }
    if (file.exists() && file.size() > 0) {
        AddRecent(path);
    }
    camera_section_->SetPreviewAllowed(isVisible());
}

void RecorderPage::ReportMissingSources() {
    const mvp::RecordingSources sources = controller_->Sources();
    QStringList missing;
    if (requested_.system_audio.enabled && !sources.system_audio) missing << QStringLiteral("\u7CFB\u7EDF\u58F0\u97F3");
    if (requested_.microphone.enabled && !sources.microphone) missing << QStringLiteral("\u9EA6\u514B\u98CE");
    if (requested_.camera.enabled && !sources.camera) missing << QStringLiteral("\u6444\u50CF\u5934");
    if (missing.isEmpty()) {
        return;
    }
    // 未能打开%1，本次录制不包含它们
    warning_label_->setText(QStringLiteral("\u672A\u80FD\u6253\u5F00%1\uFF0C\u672C\u6B21\u5F55\u5236\u4E0D\u5305\u542B"
                                           "\u5B83\u4EEC").arg(missing.join(QStringLiteral("\u3001"))));
    warning_label_->show();
}

void RecorderPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (controller_->phase() == Phase::kIdle) {
        area_section_->Refresh();
        system_row_->RefreshDevices();
        mic_row_->RefreshDevices();
        camera_section_->RefreshDevices();
        camera_section_->SetPreviewAllowed(true);
    }
    RefreshRecent();
}

void RecorderPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    camera_section_->SetPreviewAllowed(false);
}

void RecorderPage::LoadSettings() {
    QSettings settings;
    area_section_->LoadSettings(settings);
    fps_combo_->setCurrentIndex(std::max(0, fps_combo_->findData(settings.value("recorder/video/fps", 30).toInt())));
    quality_combo_->setCurrentIndex(std::clamp(settings.value("recorder/video/quality", 1).toInt(), 0, 2));
    method_combo_->setCurrentIndex(std::clamp(settings.value("recorder/video/method", 0).toInt(), 0, 2));
    cursor_box_->setChecked(settings.value("recorder/video/cursor", true).toBool());
    clicks_box_->setChecked(settings.value("recorder/video/clicks", false).toBool());
    system_row_->LoadSettings(settings, QStringLiteral("recorder/system_audio/"));
    mic_row_->LoadSettings(settings, QStringLiteral("recorder/microphone/"));
    camera_section_->LoadSettings(settings);
    const QString folder = settings.value("recorder/output/folder", DefaultFolder()).toString();
    folder_edit_->setText(QDir::toNativeSeparators(folder));
    const int container = container_combo_->findData(settings.value("recorder/output/container", "mp4").toString());
    container_combo_->setCurrentIndex(std::max(0, container));
    countdown_box_->setChecked(settings.value("recorder/output/countdown", true).toBool());
    minimize_box_->setChecked(settings.value("recorder/output/minimize", true).toBool());
}

void RecorderPage::SaveSettings() const {
    QSettings settings;
    area_section_->SaveSettings(settings);
    settings.setValue("recorder/video/fps", fps_combo_->currentData().toInt());
    settings.setValue("recorder/video/quality", quality_combo_->currentIndex());
    settings.setValue("recorder/video/encoder", encoder_combo_->currentData().toString());
    settings.setValue("recorder/video/method", method_combo_->currentIndex());
    settings.setValue("recorder/video/cursor", cursor_box_->isChecked());
    settings.setValue("recorder/video/clicks", clicks_box_->isChecked());
    system_row_->SaveSettings(settings, QStringLiteral("recorder/system_audio/"));
    mic_row_->SaveSettings(settings, QStringLiteral("recorder/microphone/"));
    camera_section_->SaveSettings(settings);
    settings.setValue("recorder/output/folder", folder_edit_->text());
    settings.setValue("recorder/output/container", container_combo_->currentData().toString());
    settings.setValue("recorder/output/countdown", countdown_box_->isChecked());
    settings.setValue("recorder/output/minimize", minimize_box_->isChecked());
}

void RecorderPage::AddRecent(const QString& path) {
    QSettings settings;
    QStringList recent = settings.value("recorder/recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent) {
        recent.removeLast();
    }
    settings.setValue("recorder/recent", recent);
    RefreshRecent();
}

void RecorderPage::RefreshRecent() {
    recent_list_->clear();
    for (const QString& path : QSettings().value("recorder/recent").toStringList()) {
        const QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }
        const QString text = QStringLiteral("%1    %2    %3")
                                 .arg(info.fileName(), shared_ui::FormatBytes(info.size()),
                                      info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        auto* item = new QListWidgetItem(text, recent_list_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    if (recent_list_->count() > 0) {
        recent_list_->setCurrentRow(0);
    }
}

QString RecorderPage::SelectedRecent() const {
    const QListWidgetItem* item = recent_list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}
