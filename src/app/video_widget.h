#ifndef MVP_APP_VIDEO_WIDGET_H_
#define MVP_APP_VIDEO_WIDGET_H_

#include <QWidget>
#include <functional>

/// Pure native-window container for SDL3 video rendering.
/// Does not perform any painting — SDL renders directly into the HWND.
class VideoWidget : public QWidget {
    Q_OBJECT

  public:
    explicit VideoWidget(QWidget* parent = nullptr);

    using ResizeCallback = std::function<void(int width, int height)>;
    void SetResizeCallback(ResizeCallback cb);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    QPaintEngine* paintEngine() const override { return nullptr; }

  private:
    ResizeCallback resize_cb_;
};

#endif  // MVP_APP_VIDEO_WIDGET_H_
