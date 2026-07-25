#include "flow_layout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int h_spacing, int v_spacing)
    : QLayout(parent), h_space_(h_spacing), v_space_(v_spacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int h_spacing, int v_spacing)
    : h_space_(h_spacing), v_space_(v_spacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    QLayoutItem* item;
    while ((item = takeAt(0))) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) { item_list_.append(item); }

int FlowLayout::horizontalSpacing() const {
    return h_space_ >= 0 ? h_space_ : SmartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
    return v_space_ >= 0 ? v_space_ : SmartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const { return item_list_.size(); }

QLayoutItem* FlowLayout::itemAt(int index) const {
    return item_list_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index < 0 || index >= item_list_.size()) {
        return nullptr;
    }
    return item_list_.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }

bool FlowLayout::hasHeightForWidth() const { return true; }

int FlowLayout::heightForWidth(int width) const {
    return DoLayout(QRect(0, 0, width, 0), /*test_only=*/true);
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    DoLayout(rect, /*test_only=*/false);
}

QSize FlowLayout::sizeHint() const { return minimumSize(); }

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : item_list_) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::SmartSpacing(QStyle::PixelMetric pm) const {
    QObject* parent_object = this->parent();
    if (!parent_object) {
        return -1;
    }
    if (parent_object->isWidgetType()) {
        auto* pw = static_cast<QWidget*>(parent_object);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    return static_cast<QLayout*>(parent_object)->spacing();
}

int FlowLayout::DoLayout(const QRect& rect, bool test_only) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effective_rect = rect.adjusted(left, top, -right, -bottom);
    int x = effective_rect.x();
    int y = effective_rect.y();
    int line_height = 0;

    for (QLayoutItem* item : item_list_) {
        int space_x = horizontalSpacing();
        int space_y = verticalSpacing();

        int next_x = x + item->sizeHint().width() + space_x;
        if (next_x - space_x > effective_rect.right() && line_height > 0) {
            x = effective_rect.x();
            y = y + line_height + space_y;
            next_x = x + item->sizeHint().width() + space_x;
            line_height = 0;
        }

        if (!test_only) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }

        x = next_x;
        line_height = qMax(line_height, item->sizeHint().height());
    }

    return y + line_height - rect.y() + bottom;
}
