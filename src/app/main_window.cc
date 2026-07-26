#include "main_window.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include <spdlog/spdlog.h>

#include "home_page.h"
#include "player_page.h"
#include "title_bar.h"
#include "transcoder_page.h"
#include "app_theme.h"

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    SetupUi();
}

void MainWindow::SetupUi() {
    setWindowTitle(QStringLiteral("MyVideoPlayer"));
    resize(ui_theme::kDefaultWindowWidth, ui_theme::kDefaultWindowHeight);
    setMinimumSize(ui_theme::kDefaultWindowWidth, ui_theme::kDefaultWindowHeight);
    setWindowFlag(Qt::FramelessWindowHint);
    setMouseTracking(true);

    // NavigationBar spans the full window height; TitleBar sits only above
    // the content column (never over the nav bar) — see app-shell-ui spec.
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(ui_theme::kResizeMargin, ui_theme::kResizeMargin,
                              ui_theme::kResizeMargin, ui_theme::kResizeMargin);
    outer->setSpacing(0);

    nav_bar_ = new NavigationBar(this);
    connect(nav_bar_, &NavigationBar::PageRequested, this, &MainWindow::NavigateTo);
    outer->addWidget(nav_bar_);

    outer->addLayout(BuildContentColumn(), 1);
}

QVBoxLayout* MainWindow::BuildContentColumn() {
    auto* column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    title_bar_ = new TitleBar(this);
    connect(title_bar_, &TitleBar::AvatarClicked, this,
            [] { SPDLOG_INFO("UI: avatar clicked (no-op placeholder)"); });
    column->addWidget(title_bar_);

    stacked_widget_ = new QStackedWidget(this);
    home_page_ = new HomePage(stacked_widget_);
    connect(home_page_, &HomePage::NavigateRequested, this, &MainWindow::NavigateTo);
    player_page_ = new PlayerPage(stacked_widget_);
    transcoder_page_ = new TranscoderPage(stacked_widget_);

    stacked_widget_->addWidget(home_page_);
    stacked_widget_->addWidget(player_page_);
    stacked_widget_->addWidget(transcoder_page_);
    column->addWidget(stacked_widget_, 1);

    // Prevent child widgets from inheriting MainWindow's edge cursor.
    // Without these, moving from a resize edge directly into a child
    // leaves the resize cursor stuck — the child has no cursor of its
    // own, so it inherits MainWindow's last setCursor() call.
    nav_bar_->setCursor(Qt::ArrowCursor);
    title_bar_->setCursor(Qt::ArrowCursor);
    stacked_widget_->setCursor(Qt::ArrowCursor);

    return column;
}

void MainWindow::NavigateTo(NavigationBar::Page page) {
    QWidget* target = home_page_;
    QString title = QStringLiteral("\u4E3B\u9875");  // 主页
    if (page == NavigationBar::Page::kPlayer) {
        target = player_page_;
        title = QStringLiteral("\u64AD\u653E\u5668");  // 播放器
    } else if (page == NavigationBar::Page::kTranscoder) {
        target = transcoder_page_;
        title = QStringLiteral("\u8F6C\u7801\u5668");  // 转码器
    }

    stacked_widget_->setCurrentWidget(target);
    title_bar_->SetPageTitle(title);
    nav_bar_->SetSelectedPage(page);
}

Qt::Edges MainWindow::ResizeEdgesAt(const QPoint& pos) const {
    const int m = ui_theme::kResizeMargin;
    Qt::Edges edges;
    if (pos.x() <= m) edges |= Qt::LeftEdge;
    if (pos.x() >= width() - m) edges |= Qt::RightEdge;
    if (pos.y() <= m) edges |= Qt::TopEdge;
    if (pos.y() >= height() - m) edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::UpdateCursor(Qt::Edges edges) {
    bool top_left_or_bottom_right = (edges & Qt::LeftEdge && edges & Qt::TopEdge) ||
                                    (edges & Qt::RightEdge && edges & Qt::BottomEdge);
    bool top_right_or_bottom_left = (edges & Qt::RightEdge && edges & Qt::TopEdge) ||
                                    (edges & Qt::LeftEdge && edges & Qt::BottomEdge);

    if (top_left_or_bottom_right) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (top_right_or_bottom_left) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    UpdateCursor(ResizeEdgesAt(event->pos()));
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        Qt::Edges edges = ResizeEdgesAt(event->pos());
        if (edges && windowHandle()) {
            windowHandle()->startSystemResize(edges);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

