#ifndef MVP_APP_ICON_BUTTON_H_
#define MVP_APP_ICON_BUTTON_H_

#include <QAbstractButton>
#include <QIcon>

/// Custom vector-painted icon button — no image/icon-font assets. Draws a
/// simple glyph (see IconKind) sized to the button's rect, with distinct
/// normal/hover/pressed background tints from ui_theme. Used for playback
/// controls, window chrome buttons, and nav group chevrons.
///
/// Optionally accepts an SVG-backed QIcon via SetCustomIcon(); when set, the
/// button renders the icon instead of the built-in QPainterPath glyph.
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

    /// Replaces the built-in QPainterPath glyph with a pre-rendered QIcon
    /// (e.g. an SVG converted to pixmap). Pass a null QIcon to restore the
    /// default vector-drawn glyph for the current IconKind.
    void SetCustomIcon(const QIcon& icon) { custom_icon_ = icon; update(); }

    /// Sets the glyph size as a fraction of the smaller button dimension.
    /// Default is 0.50 (glyph fills half the button). Window chrome buttons
    /// (min/max/close) typically use 0.28 to stay compact.
    void SetGlyphScale(qreal scale) { glyph_scale_ = scale; update(); }

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
    QIcon custom_icon_;
    qreal glyph_scale_{0.50};
    bool close_style_{false};
};

#endif  // MVP_APP_ICON_BUTTON_H_
