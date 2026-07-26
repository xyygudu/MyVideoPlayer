#include "transcoder_page.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include "icon_button.h"
#include "app_theme.h"

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

struct QualityPreset {
    int crf;
    const char* encoder_preset;
};

// 高质量 / 均衡 / 快速 — indices match quality_preset_combo_'s items.
constexpr QualityPreset kQualityPresets[3] = {
    {18, "slow"},
    {23, "medium"},
    {28, "fast"},
};

QString BuildFormStyleSheet() {
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
QPushButton#StartCancelButton {
    background: %2;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 10px 0px;
    font-weight: 600;
    font-size: 14px;
}
QPushButton#StartCancelButton:hover { background: #3D68E8; }
QPushButton#StartCancelButton:disabled { background: #C7D3F5; color: #F5F6F8; }
QProgressBar {
    border: 1px solid %1;
    border-radius: 6px;
    text-align: center;
    background: %4;
    height: 16px;
}
QProgressBar::chunk { background: %2; border-radius: 5px; }
QLabel { font-size: 12px; }
)")
        .arg(ui_theme::kBorder.name(), ui_theme::kAccent.name(), ui_theme::kAccentSoft.name(),
             ui_theme::kBgNav.name());
}

QLabel* MakeSectionHeader(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSize(11);
    label->setFont(font);
    return label;
}

}  // namespace

TranscoderPage::TranscoderPage(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ui_theme::kBgContent);
    setPalette(pal);
    setStyleSheet(BuildFormStyleSheet());
    SetupUi();
}

TranscoderPage::~TranscoderPage() = default;

void TranscoderPage::SetupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui_theme::kContentPadding, ui_theme::kContentPadding,
                               ui_theme::kContentPadding, ui_theme::kContentPadding);
    layout->setSpacing(20);
    layout->addWidget(BuildFileSection());
    layout->addWidget(BuildBasicSection());
    layout->addWidget(BuildAdvancedSection());
    layout->addWidget(BuildActionSection());
    layout->addStretch(1);
}

QWidget* TranscoderPage::BuildFileSection() {
    auto* container = new QWidget(this);
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(MakeSectionHeader(QStringLiteral("\u6587\u4EF6"), container));  // 文件

    auto* source_row = new QHBoxLayout();
    source_row->setSpacing(8);
    source_row->addWidget(new QLabel(QStringLiteral("\u8F93\u5165\u6587\u4EF6\uFF1A"), container));
    source_path_edit_ = new QLineEdit(container);
    source_path_edit_->setReadOnly(true);
    source_path_edit_->setPlaceholderText(
        QStringLiteral("\u9009\u62E9\u8981\u8F6C\u7801\u7684\u89C6\u9891\u6587\u4EF6\u2026"));
    source_row->addWidget(source_path_edit_, 1);
    browse_source_btn_ = new IconButton(IconButton::IconKind::kOpenFolder, container);
    browse_source_btn_->setFixedSize(30, 30);
    browse_source_btn_->SetCustomIcon(LoadButtonIcon(QStringLiteral(":/icons/file_open.svg"), 18));
    connect(browse_source_btn_, &QAbstractButton::clicked, this, &TranscoderPage::OnBrowseSource);
    source_row->addWidget(browse_source_btn_);
    v->addLayout(source_row);

    auto* output_row = new QHBoxLayout();
    output_row->setSpacing(8);
    output_row->addWidget(new QLabel(QStringLiteral("\u8F93\u51FA\u6587\u4EF6\uFF1A"), container));
    output_path_edit_ = new QLineEdit(container);
    output_path_edit_->setReadOnly(true);
    output_path_edit_->setPlaceholderText(
        QStringLiteral("\u9009\u62E9\u8F93\u51FA\u4F4D\u7F6E\u2026"));
    output_row->addWidget(output_path_edit_, 1);
    browse_output_btn_ = new IconButton(IconButton::IconKind::kOpenFolder, container);
    browse_output_btn_->setFixedSize(30, 30);
    browse_output_btn_->SetCustomIcon(LoadButtonIcon(QStringLiteral(":/icons/file_open.svg"), 18));
    connect(browse_output_btn_, &QAbstractButton::clicked, this, &TranscoderPage::OnBrowseOutput);
    output_row->addWidget(browse_output_btn_);
    v->addLayout(output_row);

    return container;
}

QWidget* TranscoderPage::BuildBasicSection() {
    auto* group = new QWidget(this);
    auto* v = new QVBoxLayout(group);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(MakeSectionHeader(QStringLiteral("\u57FA\u7840\u8BBE\u7F6E"), group));  // 基础设置

    auto* form = new QFormLayout();
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(12);

    container_combo_ = new QComboBox(group);
    container_combo_->addItems({"mp4", "mkv"});
    form->addRow(QStringLiteral("\u5BB9\u5668\u683C\u5F0F\uFF1A"), container_combo_);

    quality_preset_combo_ = new QComboBox(group);
    quality_preset_combo_->addItems({QStringLiteral("\u9AD8\u8D28\u91CF"),
                                     QStringLiteral("\u5747\u8861"),
                                     QStringLiteral("\u5FEB\u901F")});
    quality_preset_combo_->setCurrentIndex(1);  // 均衡 — matches EncodeParams defaults
    connect(quality_preset_combo_, &QComboBox::currentIndexChanged, this,
            &TranscoderPage::OnQualityPresetChanged);
    form->addRow(QStringLiteral("\u753B\u8D28\u9884\u8BBE\uFF1A"), quality_preset_combo_);

    v->addLayout(form);
    return group;
}

QWidget* TranscoderPage::BuildAdvancedSection() {
    auto* container = new QWidget(this);
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(BuildAdvancedHeader(container));

    advanced_body_ = BuildAdvancedForm(container);
    advanced_body_->setVisible(false);
    v->addWidget(advanced_body_);
    return container;
}

QWidget* TranscoderPage::BuildAdvancedHeader(QWidget* parent) {
    auto* header = new QWidget(parent);
    header->setFixedHeight(24);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->addWidget(
        MakeSectionHeader(QStringLiteral("\u9AD8\u7EA7\u8BBE\u7F6E"), header));  // 高级设置
    header_layout->addStretch(1);

    advanced_chevron_ = new IconButton(IconButton::IconKind::kChevronRight, header);
    advanced_chevron_->setFixedSize(18, 18);    advanced_chevron_->SetCustomIcon(
        LoadButtonIcon(QStringLiteral(":/icons/chevron_right.svg"), 16));    connect(advanced_chevron_, &QAbstractButton::clicked, this,
            &TranscoderPage::OnAdvancedToggled);
    header_layout->addWidget(advanced_chevron_);
    return header;
}

QWidget* TranscoderPage::BuildAdvancedForm(QWidget* parent) {
    auto* body = new QWidget(parent);
    advanced_form_ = new QFormLayout(body);
    advanced_form_->setVerticalSpacing(10);
    advanced_form_->setHorizontalSpacing(12);
    advanced_form_->setContentsMargins(0, 4, 0, 0);

    rate_control_combo_ = new QComboBox(body);
    rate_control_combo_->addItems({QStringLiteral("CRF\uFF08\u6052\u5B9A\u8D28\u91CF\uFF09"),
                                   QStringLiteral("\u76EE\u6807\u7801\u7387")});
    connect(rate_control_combo_, &QComboBox::currentIndexChanged, this,
            &TranscoderPage::OnRateControlModeChanged);
    advanced_form_->addRow(QStringLiteral("\u7801\u7387\u63A7\u5236\uFF1A"), rate_control_combo_);

    crf_spin_ = new QSpinBox(body);
    crf_spin_->setRange(0, 51);
    crf_spin_->setValue(23);
    advanced_form_->addRow(QStringLiteral("CRF\uFF1A"), crf_spin_);

    bitrate_spin_ = new QSpinBox(body);
    bitrate_spin_->setRange(100, 100000);
    bitrate_spin_->setValue(2000);
    bitrate_spin_->setSuffix(" kbps");
    advanced_form_->addRow(QStringLiteral("\u76EE\u6807\u7801\u7387\uFF1A"), bitrate_spin_);
    advanced_form_->setRowVisible(bitrate_spin_, false);

    gop_spin_ = new QSpinBox(body);
    gop_spin_->setRange(1, 1000);
    gop_spin_->setValue(250);
    advanced_form_->addRow(QStringLiteral("GOP \u5927\u5C0F\uFF1A"), gop_spin_);

    max_b_frames_spin_ = new QSpinBox(body);
    max_b_frames_spin_->setRange(0, 16);
    max_b_frames_spin_->setValue(2);
    advanced_form_->addRow(QStringLiteral("\u6700\u5927 B \u5E27\u6570\uFF1A"), max_b_frames_spin_);

    audio_bitrate_spin_ = new QSpinBox(body);
    audio_bitrate_spin_->setRange(32, 512);
    audio_bitrate_spin_->setValue(128);
    audio_bitrate_spin_->setSuffix(" kbps");
    advanced_form_->addRow(QStringLiteral("\u97F3\u9891\u7801\u7387\uFF1A"), audio_bitrate_spin_);

    return body;
}

QWidget* TranscoderPage::BuildActionSection() {
    auto* container = new QWidget(this);
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(0, 4, 0, 0);
    v->setSpacing(10);

    start_cancel_btn_ = new QPushButton(QStringLiteral("\u5F00\u59CB\u8F6C\u7801"), container);
    start_cancel_btn_->setObjectName(QStringLiteral("StartCancelButton"));
    start_cancel_btn_->setFixedHeight(38);
    start_cancel_btn_->setEnabled(false);
    connect(start_cancel_btn_, &QPushButton::clicked, this,
            &TranscoderPage::OnStartOrCancelClicked);
    v->addWidget(start_cancel_btn_);

    progress_bar_ = new QProgressBar(container);
    progress_bar_->setRange(0, 100);
    progress_bar_->setTextVisible(false);
    v->addWidget(progress_bar_);

    status_label_ = new QLabel(QStringLiteral("\u5C1A\u672A\u5F00\u59CB"), container);
    status_label_->setStyleSheet(QStringLiteral("color: %1;").arg(ui_theme::kTextSecondary.name()));
    v->addWidget(status_label_);

    return container;
}

void TranscoderPage::OnBrowseSource() {
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("\u9009\u62E9\u8F93\u5165\u6587\u4EF6"), QString(),
        "Video Files (*.mp4 *.avi *.mkv *.mov *.flv *.wmv);;All Files (*)");
    if (path.isEmpty()) return;
    source_path_edit_->setText(path);

    QFileInfo info(path);
    output_path_edit_->setText(info.absolutePath() + "/" + info.completeBaseName() +
                               "_transcoded." + container_combo_->currentText());
    start_cancel_btn_->setEnabled(true);
}

void TranscoderPage::OnBrowseOutput() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("\u9009\u62E9\u8F93\u51FA\u4F4D\u7F6E"), output_path_edit_->text(),
        "MP4 (*.mp4);;MKV (*.mkv)");
    if (path.isEmpty()) return;
    output_path_edit_->setText(path);
}

void TranscoderPage::OnQualityPresetChanged(int index) {
    if (index < 0 || index >= 3) return;
    crf_spin_->setValue(kQualityPresets[index].crf);
    resolved_video_preset_ = QString::fromLatin1(kQualityPresets[index].encoder_preset);
}

void TranscoderPage::OnRateControlModeChanged(int index) {
    bool is_bitrate = (index == 1);
    advanced_form_->setRowVisible(crf_spin_, !is_bitrate);
    advanced_form_->setRowVisible(bitrate_spin_, is_bitrate);
}

void TranscoderPage::OnAdvancedToggled() {
    bool now_visible = !advanced_body_->isVisible();
    advanced_body_->setVisible(now_visible);
    advanced_chevron_->SetCustomIcon(
        LoadButtonIcon(now_visible ? QStringLiteral(":/icons/chevron_down.svg")
                                   : QStringLiteral(":/icons/chevron_right.svg"),
                       16));
    advanced_chevron_->SetIconKind(now_visible ? IconButton::IconKind::kChevronDown
                                               : IconButton::IconKind::kChevronRight);
}

mvp::TranscodeOptions TranscoderPage::BuildOptionsFromUi() const {
    mvp::TranscodeOptions options;

    options.video.codec_name = "libx264";
    options.video.preset = resolved_video_preset_.toStdString();
    if (rate_control_combo_->currentIndex() == 1) {
        options.video.rate_control = mvp::RateControlMode::kBitrate;
        options.video.bitrate_bps = static_cast<int64_t>(bitrate_spin_->value()) * 1000;
    } else {
        options.video.rate_control = mvp::RateControlMode::kCrf;
        options.video.crf = crf_spin_->value();
    }
    options.video.gop_size = gop_spin_->value();
    options.video.max_b_frames = max_b_frames_spin_->value();

    options.audio.codec_name = "aac";
    options.audio.bitrate_bps = static_cast<int64_t>(audio_bitrate_spin_->value()) * 1000;

    return options;
}

void TranscoderPage::SetRunningState(bool running) {
    running_ = running;
    start_cancel_btn_->setText(running ? QStringLiteral("\u53D6\u6D88")
                                       : QStringLiteral("\u5F00\u59CB\u8F6C\u7801"));
    browse_source_btn_->setEnabled(!running);
    browse_output_btn_->setEnabled(!running);
}

void TranscoderPage::OnStartOrCancelClicked() {
    if (running_) {
        transcoder_->Cancel();
        return;
    }

    transcoder_ = std::make_unique<mvp::Transcoder>();
    transcoder_->SetInput(source_path_edit_->text().toStdString());
    transcoder_->SetOutput(output_path_edit_->text().toStdString(), BuildOptionsFromUi());

    transcoder_->SetProgressCallback([this](double pct) {
        QMetaObject::invokeMethod(this, [this, pct] { HandleProgress(pct); },
                                  Qt::QueuedConnection);
    });
    transcoder_->SetCompletionCallback([this](bool ok) {
        QMetaObject::invokeMethod(this, [this, ok] { HandleCompletion(ok); },
                                  Qt::QueuedConnection);
    });

    if (!transcoder_->Start()) {
        status_label_->setText(
            QStringLiteral("\u542F\u52A8\u5931\u8D25\uFF0C\u8BF7\u67E5\u770B\u65E5\u5FD7"));
        SPDLOG_ERROR("TranscoderPage: Transcoder::Start failed");
        return;
    }

    progress_bar_->setValue(0);
    status_label_->setText(QStringLiteral("\u8F6C\u7801\u4E2D..."));
    SetRunningState(true);
}

void TranscoderPage::HandleProgress(double percent) {
    progress_bar_->setValue(qRound(percent));
}

void TranscoderPage::HandleCompletion(bool ok) {
    progress_bar_->setValue(ok ? 100 : progress_bar_->value());
    SetRunningState(false);
    status_label_->setText(ok ? QStringLiteral("\u8F6C\u7801\u6210\u529F")
                              : QStringLiteral("\u8F6C\u7801\u5931\u8D25"));
}
