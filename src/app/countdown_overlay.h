#ifndef MVP_APP_COUNTDOWN_OVERLAY_H_
#define MVP_APP_COUNTDOWN_OVERLAY_H_

#include <QWidget>

class QScreen;
class QTimer;

/// "3, 2, 1" in the middle of a screen before a recording starts, so the
/// user can get ready. Click-through and excluded from capture.
class CountdownOverlay : public QWidget {
    Q_OBJECT

  public:
    CountdownOverlay();

    void Start(QScreen* screen, int seconds);
    void Cancel();

  signals:
    void Finished();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    void Tick();

    QTimer* timer_{nullptr};
    int remaining_{0};
};

#endif  // MVP_APP_COUNTDOWN_OVERLAY_H_
