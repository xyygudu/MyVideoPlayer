#ifndef MVP_APP_SHARED_UI_H_
#define MVP_APP_SHARED_UI_H_

#include <QColor>
#include <QIcon>
#include <QString>

#include "app_theme.h"

class QLabel;
class QWidget;

/// Helpers shared by the pages: SVG icons, section headers, the form style
/// sheet, value formatting and capture exclusion.
namespace shared_ui {

/// Renders an SVG resource drawn with `currentColor` (see
/// docs/icon-design-standard.md) into a size x size icon in `color`.
QIcon LoadSvgIcon(const QString& resource_path, int size, const QColor& color = ui_theme::kTextPrimary);

/// Bold section title of the form pages.
QLabel* MakeSectionHeader(const QString& text, QWidget* parent);

/// Style sheet of the form pages: inputs, combo and spin boxes, sliders,
/// progress bars, the primary action button (object name "PrimaryButton")
/// and secondary buttons ("SecondaryButton").
QString FormStyleSheet();

/// Makes the combo boxes, sliders and spin boxes under `root` ignore the
/// mouse wheel unless they have the focus, so scrolling a long form scrolls
/// it instead of changing whatever value passes under the cursor.
void GuardWheel(QWidget* root);

/// "01:02:03".
QString FormatDuration(double seconds);

/// "12.3 MB".
QString FormatBytes(qint64 bytes);

/// Keeps a top-level window out of screen captures (Windows 10 2004+), so
/// the recorder's own overlays never appear in a recording.
void ExcludeFromCapture(QWidget* window);

}  // namespace shared_ui

#endif  // MVP_APP_SHARED_UI_H_
