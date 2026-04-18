#ifndef MVP_APP_VIDEO_WIDGET_H_
#define MVP_APP_VIDEO_WIDGET_H_

#include <QImage>
#include <QMutex>
#include <QWidget>

class VideoWidget : public QWidget {
    Q_OBJECT

  public:
    explicit VideoWidget(QWidget* parent = nullptr);

  public slots:
    void UpdateFrame(const QImage& image);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QImage current_frame_;
    QMutex mutex_;
};

#endif  // MVP_APP_VIDEO_WIDGET_H_
