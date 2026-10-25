#include "navigation_bar.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

#include "icon_button.h"
#include "nav_item.h"
#include "app_theme.h"
#include "shared_ui.h"

namespace {

QIcon CreateNavIcon(const QString& resource_path) {
    return shared_ui::LoadSvgIcon(resource_path, ui_theme::kNavIconSize);
}

}  // namespace

NavigationBar::NavigationBar(QWidget* parent) : QWidget(parent) {
    setFixedWidth(ui_theme::kNavWidth);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ui_theme::kBgNav);
    setPalette(pal);
    SetupUi();
}

namespace {

QWidget* BuildLogoArea(QWidget* parent) {
    auto* logo_area = new QWidget(parent);
    logo_area->setFixedHeight(ui_theme::kLogoAreaHeight);
    auto* logo_layout = new QHBoxLayout(logo_area);
    logo_layout->setContentsMargins(0, 0, 8, 0);
    logo_layout->setSpacing(10);

    auto* logo_icon = new QLabel(logo_area);
    logo_icon->setFixedSize(50, 50);
    QPixmap pix(QStringLiteral(":/images/logo.png"));
    logo_icon->setPixmap(pix.scaled(50, 50, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo_layout->addWidget(logo_icon);

    auto* logo_label = new QLabel(QStringLiteral("\u97F3\u89C6\u9891\u5DE5\u5177\u7BB1"), logo_area);
    QFont logo_font = logo_label->font();
    logo_font.setPointSize(13);
    logo_font.setBold(true);
    logo_label->setFont(logo_font);
    logo_layout->addWidget(logo_label);
    return logo_area;
}

}  // namespace

void NavigationBar::SetupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(BuildLogoArea(this));

    home_item_ = new NavItem(QStringLiteral("\u4E3B\u9875"), this);  // 主页
    home_item_->SetIcon(CreateNavIcon(QStringLiteral(":/icons/home.svg")));
    connect(home_item_, &NavItem::Clicked, this, [this] { SelectItem(Page::kHome); });
    layout->addWidget(home_item_);

    layout->addWidget(BuildQuickAccessHeader());
    layout->addWidget(BuildQuickAccessBody());
    layout->addStretch(1);

    settings_item_ = new NavItem(QStringLiteral("\u8BBE\u7F6E"), this);  // 设置
    settings_item_->SetIcon(CreateNavIcon(QStringLiteral(":/icons/settings.svg")));
    connect(settings_item_, &NavItem::Clicked, this, [this] { SelectItem(Page::kSettings); });
    layout->addWidget(settings_item_);
    layout->addSpacing(12);

    home_item_->SetSelected(true);
}

QWidget* NavigationBar::BuildQuickAccessHeader() {
    quick_access_header_ = new QWidget(this);
    quick_access_header_->setFixedHeight(28);
    quick_access_header_->installEventFilter(this);
    auto* qa_layout = new QHBoxLayout(quick_access_header_);
    qa_layout->setContentsMargins(ui_theme::kNavItemLeftPadding, 0, 8, 0);

    auto* qa_label = new QLabel(QStringLiteral("\u5FEB\u901F\u8BBF\u95EE"),  // 快速访问
                                quick_access_header_);
    QFont label_font = qa_label->font();
    label_font.setPointSize(9);
    qa_label->setFont(label_font);
    qa_label->setStyleSheet(QStringLiteral("color: %1;").arg(ui_theme::kTextSecondary.name()));
    qa_layout->addWidget(qa_label);
    qa_layout->addStretch(1);

    quick_access_chevron_ =
        new IconButton(IconButton::IconKind::kChevronDown, quick_access_header_);
    quick_access_chevron_->setFixedSize(16, 16);
    quick_access_chevron_->SetCustomIcon(
        CreateNavIcon(QStringLiteral(":/icons/chevron_down.svg")));
    connect(quick_access_chevron_, &QAbstractButton::clicked, this,
            &NavigationBar::OnQuickAccessToggled);
    qa_layout->addWidget(quick_access_chevron_);
    return quick_access_header_;
}

QWidget* NavigationBar::BuildQuickAccessBody() {
    quick_access_body_ = new QWidget(this);
    auto* qa_body_layout = new QVBoxLayout(quick_access_body_);
    qa_body_layout->setContentsMargins(0, 0, 0, 0);
    qa_body_layout->setSpacing(0);

    player_item_ = new NavItem(QStringLiteral("\u64AD\u653E\u5668"), quick_access_body_);  // 播放器
    player_item_->SetIcon(CreateNavIcon(QStringLiteral(":/icons/player.svg")));
    connect(player_item_, &NavItem::Clicked, this, [this] { SelectItem(Page::kPlayer); });
    qa_body_layout->addWidget(player_item_);

    transcoder_item_ =
        new NavItem(QStringLiteral("\u8F6C\u7801\u5668"), quick_access_body_);  // 转码器
    transcoder_item_->SetIcon(CreateNavIcon(QStringLiteral(":/icons/converter.svg")));
    connect(transcoder_item_, &NavItem::Clicked, this, [this] { SelectItem(Page::kTranscoder); });
    qa_body_layout->addWidget(transcoder_item_);

    recorder_item_ = new NavItem(QStringLiteral("\u5F55\u5C4F"), quick_access_body_);  // 录屏
    recorder_item_->SetIcon(CreateNavIcon(QStringLiteral(":/icons/recorder.svg")));
    connect(recorder_item_, &NavItem::Clicked, this, [this] { SelectItem(Page::kRecorder); });
    qa_body_layout->addWidget(recorder_item_);

    return quick_access_body_;
}

void NavigationBar::SelectItem(Page page) {
    UpdateSelection(page);
    emit PageRequested(page);
}

void NavigationBar::SetSelectedPage(Page page) {
    UpdateSelection(page);
}

void NavigationBar::UpdateSelection(Page page) {
    home_item_->SetSelected(page == Page::kHome);
    player_item_->SetSelected(page == Page::kPlayer);
    transcoder_item_->SetSelected(page == Page::kTranscoder);
    recorder_item_->SetSelected(page == Page::kRecorder);
    settings_item_->SetSelected(page == Page::kSettings);
    current_page_ = page;
}

void NavigationBar::OnQuickAccessToggled() {
    bool now_visible = !quick_access_body_->isVisible();
    quick_access_body_->setVisible(now_visible);
    quick_access_chevron_->SetCustomIcon(
        CreateNavIcon(now_visible ? QStringLiteral(":/icons/chevron_down.svg")
                                 : QStringLiteral(":/icons/chevron_right.svg")));
    quick_access_chevron_->SetIconKind(now_visible ? IconButton::IconKind::kChevronDown
                                                   : IconButton::IconKind::kChevronRight);
}

bool NavigationBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == quick_access_header_ && event->type() == QEvent::MouseButtonPress) {
        OnQuickAccessToggled();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
