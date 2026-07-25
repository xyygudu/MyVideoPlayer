#ifndef MVP_APP_ICON_BUTTON_H_
#define MVP_APP_ICON_BUTTON_H_

#include <QAbstractButton>

/// Custom vector-painted icon button — no image/icon-font assets. Draws a
/// simple glyph (see IconKind) sized to the button's rect, with distinct
/// normal/hover/pressed background tints from ui_theme. Used for playback
/// controls, window chrome buttons, and nav group chevrons.
class IconButton : public QAbstractButton {
    Q_OBJECT

  public:
    enum class IconKind {
        kPlay,
        kPause,
        kOpenFolder,
        kMinimize,
        kMaximize,
        kRestore,
        kClose,
        kChevronDown,
        kChevronRight,
    };

    explicit IconButton(IconKind kind, QWidget* parent = nullptr);

    void SetIconKind(IconKind kind);
    IconKind GetIconKind() const { return kind_; }

    /// When true, hover/press use ui_theme::kCloseHover instead of
    /// kHoverTint (used by the window close button).
    void SetCloseStyle(bool close_style) { close_style_ = close_style; }

    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    void PaintBackground(class QPainter& painter) const;
    void PaintGlyph(class QPainter& painter, const QRectF& glyph_rect) const;

    IconKind kind_;
    bool close_style_{false};
};

#endif  // MVP_APP_ICON_BUTTON_H_
