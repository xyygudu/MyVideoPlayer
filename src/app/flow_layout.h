#ifndef MVP_APP_FLOW_LAYOUT_H_
#define MVP_APP_FLOW_LAYOUT_H_

#include <QLayout>
#include <QList>
#include <QStyle>

/// Standard Qt "Flow Layout" pattern: lays out child widgets left-to-right,
/// wrapping to the next row when a row runs out of horizontal space.
/// Used by HomePage so dashboard cards reflow as the window is resized.
class FlowLayout : public QLayout {
  public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int h_spacing = -1,
                        int v_spacing = -1);
    explicit FlowLayout(int margin = -1, int h_spacing = -1, int v_spacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

  private:
    int DoLayout(const QRect& rect, bool test_only) const;
    int SmartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> item_list_;
    int h_space_;
    int v_space_;
};

#endif  // MVP_APP_FLOW_LAYOUT_H_
