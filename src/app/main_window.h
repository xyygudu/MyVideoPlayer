#ifndef MVP_APP_MAIN_WINDOW_H_
#define MVP_APP_MAIN_WINDOW_H_

#include <QWidget>

#include "navigation_bar.h"

class TitleBar;
class QStackedWidget;
class QVBoxLayout;
class HomePage;
class PlayerPage;
class TranscoderPage;

/// Frameless application shell: `NavigationBar` spans the full window
/// height on the left; to its right, `TitleBar` sits above a
/// `QStackedWidget` (Home/Player/Transcoder, all persistent — never
/// recreated on navigation) so the title bar never extends over the nav
/// column. See the app-shell-ui spec for full requirements (drag-to-move/
/// resize, window controls, navigation).
class MainWindow : public QWidget {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

  private:
    void SetupUi();
    QVBoxLayout* BuildContentColumn();
    void NavigateTo(NavigationBar::Page page);
    Qt::Edges ResizeEdgesAt(const QPoint& pos) const;
    void UpdateCursor(Qt::Edges edges);

    TitleBar* title_bar_{nullptr};
    NavigationBar* nav_bar_{nullptr};
    QStackedWidget* stacked_widget_{nullptr};
    HomePage* home_page_{nullptr};
    PlayerPage* player_page_{nullptr};
    TranscoderPage* transcoder_page_{nullptr};
};

#endif  // MVP_APP_MAIN_WINDOW_H_


