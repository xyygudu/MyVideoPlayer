## Context

The MyVideoPlayer app currently has no icon resource system. All icons are drawn at runtime via `QPainterPath` inside `IconButton` (9 icon kinds for transport controls, window controls, and chevrons). The `NavItem` class already exposes a `SetIcon(QIcon)` method with a reserved 18×18px slot, but no icons are assigned to the three nav items (主页/播放器/转码器). The home dashboard cards (`DashboardCard`) use custom `QPainter` lambdas (`PaintPlayGlyph`, `PaintConvertGlyph`) that approximate the same shapes.

We have 11 SVG icons in `src/app/resources/icons/` designed with a unified rounded style matching the project's `#4F7CFF` accent color. They use `currentColor` for fill/stroke, making them themeable via `QIcon::setIsMask(true)` or `QPainter::setPen()`.

The key constraint is that **`Qt6::Svg` is not currently linked**, and **no `.qrc` resource file exists** in the project. Both are needed to load SVGs from the binary.

## Goals / Non-Goals

**Goals:**
- Create a `.qrc` resource file compiling all 11 SVGs into the binary as `:/icons/<name>.svg`
- Add `Qt6::Svg` dependency to the app's CMake target
- Set nav bar icons via `NavItem::SetIcon()`: home, player, converter
- Upgrade home dashboard card thumbnails to use SVG-backed `QIcon` rendering
- Add a `kConverter` icon kind to `IconButton` (reusing the converter SVG) for parity

**Non-Goals:**
- Replacing the existing `IconButton` vector paths (play, pause, folder, window controls remain as-is — they work well and are trivial shapes)
- Adding new UI controls (stop, volume, skip prev/next buttons — these are separate features, not icon integration)
- Dark mode or theme switching support
- Converting window control icons (minimize/maximize/restore/close) to SVG — these are too small (40×40 buttons with tiny glyphs) to benefit from SVG

## Decisions

### Decision 1: `.qrc` resource file with `qt_add_resources()` (not runtime file loading)

- **Option A** (chosen): Create `src/app/resources/icons.qrc` and use `qt_add_resources(app_target icons src/app/resources/icons.qrc)`. SVGs are compiled into the binary, accessible via `QIcon(":/icons/home.svg")`. No file path dependencies at runtime.
- **Option B**: Load SVGs from disk at startup using `QSvgRenderer` or `QIcon::fromTheme()`. Rejected because: file paths break after install/deployment; no benefit for static assets.

### Decision 2: `QIcon::fromTheme` not used — direct resource path

`QIcon(":/icons/home.svg")` is simpler and more explicit than setting up a freedesktop.org theme hierarchy. Qt's SVG plugin handles rendering automatically when `Qt6::Svg` is linked.

### Decision 3: Dashboard card thumbnails — `QIcon::paint()` via `QPixmap` cache

The existing `DashboardCard::paintEvent` uses a `ThumbnailPainter` std::function lambda. We replace these with `QIcon` objects that paint via `QPixmap` (cached once on construction, reused on every paint). This avoids per-frame SVG parsing while keeping the existing card painting architecture.

### Decision 4: `IconButton` new kind via enum, not separate class

Adding `kConverter` to the existing `IconButton::IconKind` enum and adding a QPainterPath branch is the simplest approach. No need for a new widget class — `IconButton` already handles hover/press states, sizing, and color.

## Risks / Trade-offs

- **[Risk]** `Qt6::Svg` is an additional Qt module dependency. If not found at configure time, the build fails with a clear error. → **Mitigation**: Use `find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)` so CMake fails early with a standard "not found" message.
- **[Risk]** Resource file changes require a CMake reconfigure (`cmake --preset default`) before they're picked up. → **Mitigation**: The project already uses `CONFIGURE_DEPENDS` for GLOB_RECURSE in the media library; adding `qt_add_resources` triggers reconfigure automatically when `.qrc` or listed files change.
- **[Trade-off]** Dashboard card thumbnails currently use a solid `kAccentSoft` background with a centered white glyph. SVG icons have transparent backgrounds — they'll look slightly different in the card thumbnail if not rendered with the same background fill. → **Accept**: The SVG icons are more detailed and professional; the slight visual change is an improvement.
