#include "title_bar.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWindow>

#include "icon_button.h"
#include "app_theme.h"

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(ui_theme::kTitleBarHeight);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ui_theme::kBgContent);
    setPalette(pal);
    SetupUi();
}

void TitleBar::SetupUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 0, 0);
    layout->setSpacing(0);

    title_label_ = new QLabel(QStringLiteral("\u4E3B\u9875"), this);  // "主页"
    QFont title_font = title_label_->font();
    title_font.setPointSize(13);
    title_font.setBold(true);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_);
    layout->addStretch(1);

    avatar_label_ = MakeAvatarLabel();
    layout->addWidget(avatar_label_);
    layout->addSpacing(12);

    minimize_button_ = new IconButton(IconButton::IconKind::kMinimize, this);
    maximize_button_ = new IconButton(IconButton::IconKind::kMaximize, this);
    close_button_ = new IconButton(IconButton::IconKind::kClose, this);
    close_button_->SetCloseStyle(true);

    for (auto* btn : {minimize_button_, maximize_button_, close_button_}) {
        btn->setFixedSize(ui_theme::kWindowButtonWidth, ui_theme::kTitleBarHeight);
        layout->addWidget(btn);
    }

    connect(minimize_button_, &QAbstractButton::clicked, this, &TitleBar::OnMinimizeClicked);
    connect(maximize_button_, &QAbstractButton::clicked, this,
            &TitleBar::OnMaximizeRestoreClicked);
    connect(close_button_, &QAbstractButton::clicked, this, &TitleBar::OnCloseClicked);
}

QLabel* TitleBar::MakeAvatarLabel() {
    auto* label = new QLabel(this);
    label->setFixedSize(ui_theme::kAvatarSize, ui_theme::kAvatarSize);
    label->setCursor(Qt::PointingHandCursor);

    const qreal s = ui_theme::kAvatarSize;
    QPixmap pixmap(ui_theme::kAvatarSize, ui_theme::kAvatarSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ui_theme::kAccentSoft);
    painter.drawEllipse(pixmap.rect());

    QPainterPath clip;
    clip.addEllipse(pixmap.rect());
    painter.setClipPath(clip);
    painter.setBrush(ui_theme::kAccent);
    painter.drawEllipse(QRectF(s * 0.32, s * 0.18, s * 0.36, s * 0.36));  // head
    painter.drawEllipse(QRectF(s * 0.08, s * 0.55, s * 0.84, s * 0.6));   // shoulders
    painter.end();

    label->setPixmap(pixmap);
    label->installEventFilter(this);
    return label;
}

void TitleBar::SetPageTitle(const QString& title) { title_label_->setText(title); }

void TitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        OnMaximizeRestoreClicked();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateMaximizeIcon();
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == avatar_label_ && event->type() == QEvent::MouseButtonPress) {
        emit AvatarClicked();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TitleBar::OnMinimizeClicked() { window()->showMinimized(); }

void TitleBar::OnMaximizeRestoreClicked() {
    if (window()->isMaximized()) {
        window()->showNormal();
    } else {
        window()->showMaximized();
    }
    UpdateMaximizeIcon();
}

void TitleBar::OnCloseClicked() { window()->close(); }

void TitleBar::UpdateMaximizeIcon() {
    maximize_button_->SetIconKind(window()->isMaximized() ? IconButton::IconKind::kRestore
                                                          : IconButton::IconKind::kMaximize);
}
