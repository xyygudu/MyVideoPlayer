#ifndef MVP_APP_RECORDING_TOOLBAR_H_
#define MVP_APP_RECORDING_TOOLBAR_H_

#include <QPoint>
#include <QWidget>

class IconButton;
class QLabel;
class QScreen;

/// The floating strip shown while recording, like Bandicam's or Xbox Game
/// Bar's: a red dot, the recorded time, pause/resume and stop. Frameless,
/// always on top, draggable, and excluded from capture.
class RecordingToolbar : public QWidget {
    Q_OBJECT

  public:
    RecordingToolbar();

    /// Shows it at the top center of `screen`.
    void ShowOn(QScreen* screen);
    void SetElapsed(double seconds);
    void SetPaused(bool paused);

  signals:
    void PauseResumeClicked();
    void StopClicked();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

  private:
    QLabel* time_label_{nullptr};
    IconButton* pause_button_{nullptr};
    IconButton* stop_button_{nullptr};
    bool paused_{false};
    QPoint drag_offset_;
};

#endif  // MVP_APP_RECORDING_TOOLBAR_H_
