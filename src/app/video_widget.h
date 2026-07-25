#ifndef MVP_APP_VIDEO_WIDGET_H_
#define MVP_APP_VIDEO_WIDGET_H_

#include <QWidget>
#include <functional>

/// SDL3 video rendering surface. When no video is playing, Qt fills the
/// widget with black (paintEvent). Once SDL starts rendering (SetVideoPlaying
/// is called with true), WA_PaintOnScreen is enabled so Qt stops painting
/// and SDL owns the surface entirely — no Win32 API needed.
class VideoWidget : public QWidget {
    Q_OBJECT

  public:
    explicit VideoWidget(QWidget* parent = nullptr);

    using ResizeCallback = std::function<void(int width, int height)>;
    void SetResizeCallback(ResizeCallback cb);

    /// Toggles between Qt-painted black (false, default) and SDL-owned
    /// surface (true). Call before/after opening a video file so the
    /// widget never shows stale content from a previous page.
    void SetVideoPlaying(bool playing);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    QPaintEngine* paintEngine() const override { return nullptr; }

  private:
    ResizeCallback resize_cb_;
    bool has_video_{false};
};

#endif  // MVP_APP_VIDEO_WIDGET_H_
