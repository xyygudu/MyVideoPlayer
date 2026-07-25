#ifndef MVP_APP_TITLE_BAR_H_
#define MVP_APP_TITLE_BAR_H_

#include <QWidget>

class QLabel;
class IconButton;

/// Custom title bar replacing the native OS title bar: page title (left),
/// avatar placeholder + window control buttons (right, see ui_theme for
/// dimensions). Drag-to-move uses QWindow::startSystemMove(); double-click
/// on an empty area toggles maximize/restore.
class TitleBar : public QWidget {
    Q_OBJECT

  public:
    explicit TitleBar(QWidget* parent = nullptr);

    void SetPageTitle(const QString& title);

  signals:
    /// Emitted when the (currently decorative) avatar is clicked.
    void AvatarClicked();

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void OnMinimizeClicked();
    void OnMaximizeRestoreClicked();
    void OnCloseClicked();

  private:
    void SetupUi();
    void UpdateMaximizeIcon();
    QLabel* MakeAvatarLabel();

    QLabel* title_label_{nullptr};
    QLabel* avatar_label_{nullptr};
    IconButton* minimize_button_{nullptr};
    IconButton* maximize_button_{nullptr};
    IconButton* close_button_{nullptr};
};

#endif  // MVP_APP_TITLE_BAR_H_
