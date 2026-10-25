#include "settings_page.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "app_theme.h"
#include "mvp/logging.h"
#include "mvp/media_player.h"

namespace {

constexpr int kDiagnosticsRefreshMs = 500;

constexpr char kKeyHardwareDecode[] = "pipeline/hardware_decode";
constexpr char kKeyGpuEffects[] = "pipeline/gpu_effects";
constexpr char kKeyZeroCopy[] = "pipeline/zero_copy";
constexpr char kKeyLogLevel[] = "logging/level";

struct LogLevelEntry {
    mvp::logging::Level level;
    const char* name;
};

constexpr LogLevelEntry kLogLevels[] = {
    {mvp::logging::Level::kTrace, "Trace"}, {mvp::logging::Level::kDebug, "Debug"},
    {mvp::logging::Level::kInfo, "Info"},   {mvp::logging::Level::kWarn, "Warn"},
    {mvp::logging::Level::kError, "Error"},
};

QString StateName(mvp::PlaybackState state) {
    switch (state) {
        case mvp::PlaybackState::kIdle: return QStringLiteral("\u7A7A\u95F2");               // 空闲
        case mvp::PlaybackState::kReady: return QStringLiteral("\u5C31\u7EEA");              // 就绪
        case mvp::PlaybackState::kPlaying: return QStringLiteral("\u64AD\u653E\u4E2D");      // 播放中
        case mvp::PlaybackState::kPaused: return QStringLiteral("\u5DF2\u6682\u505C");       // 已暂停
        case mvp::PlaybackState::kFinished: return QStringLiteral("\u64AD\u653E\u7ED3\u675F");  // 播放结束
        case mvp::PlaybackState::kError: return QStringLiteral("\u9519\u8BEF");              // 错误
    }
    return {};
}

QLabel* MakeNote(const QString& text, QWidget* parent) {
    auto* note = new QLabel(text, parent);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(ui_theme::kTextSecondary.name()));
    return note;
}

}  // namespace

SettingsPage::SettingsPage(mvp::MediaPlayer* player, QWidget* parent)
    : QWidget(parent), player_(player) {
    auto* content = new QWidget(this);
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(24, 16, 24, 16);
    column->setSpacing(12);
    column->addWidget(BuildPipelineGroup());
    column->addWidget(BuildDiagnosticsGroup());
    column->addWidget(BuildLoggingGroup());
    column->addWidget(BuildDebugGroup());
    column->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &SettingsPage::RefreshDiagnostics);
    LoadSettings();
}

QWidget* SettingsPage::BuildPipelineGroup() {
    auto* group = new QGroupBox(QStringLiteral("\u89E3\u7801\u4E0E\u6E32\u67D3"), this);  // 解码与渲染
    auto* layout = new QVBoxLayout(group);
    hw_decode_box_ = new QCheckBox(QStringLiteral("\u786C\u4EF6\u89E3\u7801 (D3D11VA)"), group);  // 硬件解码
    // 关闭后使用 CPU 软件解码
    hw_decode_box_->setToolTip(QStringLiteral("\u5173\u95ED\u540E\u4F7F\u7528 CPU \u8F6F\u4EF6\u89E3\u7801"));
    gpu_effects_box_ = new QCheckBox(QStringLiteral("GPU \u7279\u6548\u5904\u7406"), group);  // GPU 特效处理
    // 关闭后特效在 CPU 上执行（硬件帧先下载到内存）
    gpu_effects_box_->setToolTip(QStringLiteral("\u5173\u95ED\u540E\u7279\u6548\u5728 CPU \u4E0A\u6267\u884C"
                                                "\uFF08\u786C\u4EF6\u5E27\u5148\u4E0B\u8F7D\u5230\u5185\u5B58\uFF09"));
    zero_copy_box_ = new QCheckBox(QStringLiteral("\u786C\u4EF6\u5E27\u96F6\u62F7\u8D1D\u5448\u73B0"), group);
    // 关闭后硬件帧先下载到内存，再上传为纹理
    zero_copy_box_->setToolTip(QStringLiteral("\u5173\u95ED\u540E\u786C\u4EF6\u5E27\u5148\u4E0B\u8F7D\u5230"
                                              "\u5185\u5B58\uFF0C\u518D\u4E0A\u4F20\u4E3A\u7EB9\u7406"));
    for (QCheckBox* box : {hw_decode_box_, gpu_effects_box_, zero_copy_box_}) {
        layout->addWidget(box);
        connect(box, &QCheckBox::toggled, this, &SettingsPage::ApplyPipelineOptions);
    }
    // 修改立即生效：已打开的文件会在当前位置重建播放管线。
    layout->addWidget(MakeNote(QStringLiteral("\u4FEE\u6539\u7ACB\u5373\u751F\u6548\uFF1A\u5DF2\u6253\u5F00"
                                              "\u7684\u6587\u4EF6\u4F1A\u5728\u5F53\u524D\u4F4D\u7F6E\u91CD"
                                              "\u5EFA\u64AD\u653E\u7BA1\u7EBF\u3002"),
                               group));
    return group;
}

QWidget* SettingsPage::BuildDiagnosticsGroup() {
    auto* group = new QGroupBox(QStringLiteral("\u5F53\u524D\u7BA1\u7EBF"), this);  // 当前管线
    auto* form = new QFormLayout(group);
    auto add_row = [group, form](const QString& name) {
        auto* value = new QLabel(group);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(name, value);
        return value;
    };
    state_label_ = add_row(QStringLiteral("\u64AD\u653E\u72B6\u6001"));           // 播放状态
    codec_label_ = add_row(QStringLiteral("\u89C6\u9891\u7F16\u7801"));           // 视频编码
    decoder_label_ = add_row(QStringLiteral("\u89E3\u7801\u65B9\u5F0F"));         // 解码方式
    renderer_label_ = add_row(QStringLiteral("\u6E32\u67D3\u540E\u7AEF"));        // 渲染后端
    frames_label_ = add_row(QStringLiteral("\u5DF2\u5448\u73B0\u5E27\u6570"));    // 已呈现帧数
    position_label_ = add_row(QStringLiteral("\u64AD\u653E\u4F4D\u7F6E"));        // 播放位置
    return group;
}

QWidget* SettingsPage::BuildLoggingGroup() {
    auto* group = new QGroupBox(QStringLiteral("\u65E5\u5FD7"), this);  // 日志
    auto* form = new QFormLayout(group);
    log_level_combo_ = new QComboBox(group);
    for (const LogLevelEntry& entry : kLogLevels) {
        log_level_combo_->addItem(QString::fromLatin1(entry.name), static_cast<int>(entry.level));
    }
    connect(log_level_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::ApplyLogLevel);
    form->addRow(QStringLiteral("\u65E5\u5FD7\u7EA7\u522B"), log_level_combo_);  // 日志级别

    console_box_ = new QCheckBox(QStringLiteral("\u8F93\u51FA\u5230\u63A7\u5236\u53F0"), group);  // 输出到控制台
    connect(console_box_, &QCheckBox::toggled, this,
            [](bool enabled) { mvp::logging::SetConsoleLoggingEnabled(enabled); });
    form->addRow(console_box_);

    const QString path = QString::fromStdString(mvp::logging::FileLogPath());
    auto* path_label = new QLabel(path.isEmpty() ? QStringLiteral("\u672A\u542F\u7528")  // 未启用
                                                 : QDir::toNativeSeparators(path),
                                  group);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_label->setWordWrap(true);
    form->addRow(QStringLiteral("\u65E5\u5FD7\u6587\u4EF6"), path_label);  // 日志文件

    auto* open_dir = new QPushButton(QStringLiteral("\u6253\u5F00\u65E5\u5FD7\u76EE\u5F55"), group);  // 打开日志目录
    open_dir->setEnabled(!path.isEmpty());
    connect(open_dir, &QPushButton::clicked, this, [path] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    form->addRow(open_dir);
    return group;
}

QWidget* SettingsPage::BuildDebugGroup() {
    auto* group = new QGroupBox(QStringLiteral("\u8C03\u8BD5"), this);  // 调试
    auto* layout = new QVBoxLayout(group);
    // 保存当前帧截图…
    auto* capture = new QPushButton(QStringLiteral("\u4FDD\u5B58\u5F53\u524D\u5E27\u622A\u56FE\u2026"), group);
    connect(capture, &QPushButton::clicked, this, &SettingsPage::SaveScreenshot);
    layout->addWidget(capture, 0, Qt::AlignLeft);
    debug_status_label_ = MakeNote(QString(), group);
    debug_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(debug_status_label_);
    return group;
}

void SettingsPage::LoadSettings() {
    QSettings settings;
    const mvp::PlayerOptions defaults;
    {
        const QSignalBlocker b1(hw_decode_box_), b2(gpu_effects_box_), b3(zero_copy_box_);
        hw_decode_box_->setChecked(settings.value(kKeyHardwareDecode, defaults.hardware_decode).toBool());
        gpu_effects_box_->setChecked(settings.value(kKeyGpuEffects, defaults.gpu_effects).toBool());
        zero_copy_box_->setChecked(settings.value(kKeyZeroCopy, defaults.zero_copy).toBool());
    }
    ApplyPipelineOptions();

    const int saved_level = settings.value(kKeyLogLevel, static_cast<int>(mvp::logging::GetLevel())).toInt();
    const int index = std::max(0, log_level_combo_->findData(saved_level));
    {
        const QSignalBlocker blocker(log_level_combo_);
        log_level_combo_->setCurrentIndex(index);
    }
    ApplyLogLevel(index);

    const QSignalBlocker blocker(console_box_);
    console_box_->setChecked(mvp::logging::IsConsoleLoggingEnabled());
}

void SettingsPage::ApplyPipelineOptions() {
    mvp::PlayerOptions options;
    options.hardware_decode = hw_decode_box_->isChecked();
    options.gpu_effects = gpu_effects_box_->isChecked();
    options.zero_copy = zero_copy_box_->isChecked();
    // Both switches only concern hardware frames.
    gpu_effects_box_->setEnabled(options.hardware_decode);
    zero_copy_box_->setEnabled(options.hardware_decode);

    QSettings settings;
    settings.setValue(kKeyHardwareDecode, options.hardware_decode);
    settings.setValue(kKeyGpuEffects, options.gpu_effects);
    settings.setValue(kKeyZeroCopy, options.zero_copy);

    SPDLOG_INFO("UI: player options hardware_decode={} gpu_effects={} zero_copy={}", options.hardware_decode,
                options.gpu_effects, options.zero_copy);
    player_->SetOptions(options);
    RefreshDiagnostics();
}

void SettingsPage::ApplyLogLevel(int index) {
    const int level = log_level_combo_->itemData(index).toInt();
    mvp::logging::SetLevel(static_cast<mvp::logging::Level>(level));
    QSettings().setValue(kKeyLogLevel, level);
}

void SettingsPage::RefreshDiagnostics() {
    const mvp::PipelineInfo info = player_->GetPipelineInfo();
    const QString none = QStringLiteral("\u65E0");  // 无
    const bool has_video = !info.video_codec.empty();
    state_label_->setText(StateName(player_->State()));
    codec_label_->setText(has_video ? QString::fromStdString(info.video_codec) : none);
    if (!has_video) {
        decoder_label_->setText(none);
    } else if (info.hardware_decoding) {
        decoder_label_->setText(QStringLiteral("GPU (D3D11VA)"));
    } else {
        decoder_label_->setText(QStringLiteral("CPU (\u8F6F\u4EF6\u89E3\u7801)"));  // CPU (软件解码)
    }
    renderer_label_->setText(info.renderer.empty() ? none : QString::fromStdString(info.renderer));
    frames_label_->setText(QString::number(info.frames_presented));
    position_label_->setText(QStringLiteral("%1 / %2 s   %3 fps")
                                 .arg(player_->CurrentPosition(), 0, 'f', 3)
                                 .arg(player_->Duration(), 0, 'f', 3)
                                 .arg(player_->VideoFps(), 0, 'f', 2));
}

void SettingsPage::SaveScreenshot() {
    const mvp::CapturedFrame frame = player_->CaptureFrame();
    if (frame.width == 0) {
        debug_status_label_->setText(QStringLiteral("\u6CA1\u6709\u53EF\u622A\u56FE\u7684\u753B\u9762"));  // 没有可截图的画面
        return;
    }
    const QString suggested = QStringLiteral("frame_%1.png").arg(frame.pts, 0, 'f', 3);
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("\u4FDD\u5B58\u622A\u56FE"),  // 保存截图
                                                      suggested,
                                                      QStringLiteral("PNG \u56FE\u7247 (*.png)"));  // PNG 图片
    if (path.isEmpty()) {
        return;
    }
    const QImage image(frame.rgba.data(), frame.width, frame.height, frame.width * 4, QImage::Format_RGBA8888);
    const bool saved = image.save(path, "PNG");
    const QString prefix = saved ? QStringLiteral("\u5DF2\u4FDD\u5B58\uFF1A")    // 已保存：
                                 : QStringLiteral("\u4FDD\u5B58\u5931\u8D25\uFF1A");  // 保存失败：
    debug_status_label_->setText(prefix + QDir::toNativeSeparators(path));
}

void SettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    RefreshDiagnostics();
    refresh_timer_->start(kDiagnosticsRefreshMs);
}

void SettingsPage::hideEvent(QHideEvent* event) {
    refresh_timer_->stop();
    QWidget::hideEvent(event);
}
