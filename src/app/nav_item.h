#ifndef MVP_APP_NAV_ITEM_H_
#define MVP_APP_NAV_ITEM_H_

#include <QIcon>
#include <QString>
#include <QWidget>

/// A single navigation menu entry: a reserved icon slot (blank until a
/// real icon is supplied via SetIcon — see ui_theme::kNavIconSize) followed
/// by a text label, with a selected accent bar + tint and a hover tint.
/// All items (top-level "主页" and quick-access "播放器"/"转码器") share
/// the same left indent so their icons/text columns line up.
class NavItem : public QWidget {
    Q_OBJECT

  public:
    explicit NavItem(const QString& text, QWidget* parent = nullptr);

    void SetSelected(bool selected);
    bool IsSelected() const { return selected_; }

    /// Sets the icon drawn in the reserved icon slot. Passing a null QIcon
    /// (the default) leaves the slot blank but still reserves its space,
    /// so alignment doesn't shift once real icons are supplied later.
    void SetIcon(const QIcon& icon);

  signals:
    void Clicked();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    QString text_;
    QIcon icon_;
    bool selected_{false};
};

#endif  // MVP_APP_NAV_ITEM_H_

