#ifndef MVP_APP_DASHBOARD_CARD_H_
#define MVP_APP_DASHBOARD_CARD_H_

#include <functional>

#include <QString>
#include <QWidget>

class QGraphicsDropShadowEffect;

/// A single Home-page tool card: a custom-painted thumbnail area on top,
/// title + description below. Fixed size (see ui_theme::kCardWidth/
/// kCardHeight). Hover deepens the drop shadow and lifts the drawn content
/// 2px upward (via QGraphicsDropShadowEffect + a paint-time translate —
/// the widget's own geometry never changes, so this is safe inside a
/// FlowLayout-managed grid). Click emits Clicked().
class DashboardCard : public QWidget {
    Q_OBJECT

  public:
    /// Draws the card's thumbnail glyph into the given rect. Keeps
    /// DashboardCard generic — callers (Home page) supply their own
    /// glyph-drawing lambda instead of this class hardcoding icons.
    using ThumbnailPainter = std::function<void(QPainter&, const QRectF&)>;

    DashboardCard(QString title, QString description, ThumbnailPainter thumbnail_painter,
                 QWidget* parent = nullptr);

  signals:
    void Clicked();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    void PaintThumbnail(QPainter& painter) const;
    void PaintTextArea(QPainter& painter) const;

    QString title_;
    QString description_;
    ThumbnailPainter thumbnail_painter_;
    bool hovered_{false};
    QGraphicsDropShadowEffect* shadow_effect_{nullptr};
};

#endif  // MVP_APP_DASHBOARD_CARD_H_
