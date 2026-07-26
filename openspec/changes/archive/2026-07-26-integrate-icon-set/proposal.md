## Why

The navigation bar, home dashboard cards, and player controls all use text-only labels or hand-painted `QPainter` glyphs for their icons. While functional, the lack of cohesive, unified visual branding makes the UI feel unfinished. A consistent icon set improves usability (visual scanning), polish, and brand identity — especially important for a media player where transport controls are the primary interaction.

We already have a complete set of 11 SVG icons (`resources/icons/*.svg`) designed in a unified rounded style matching the project's color palette (`#4F7CFF` accent). This change integrates them into the app.

## What Changes

- Create a Qt resource file (`.qrc`) to compile SVGs into the binary
- Add `Qt6::Svg` dependency and resource compilation to the app's CMake
- Set nav bar icons: `home.svg` → "主页", `player.svg` → "播放器", `converter.svg` → "转码器"
- Upgrade home dashboard card thumbnails from custom `QPainter` lambdas to the same SVG icons
- Add a new `kConverter` icon kind to `IconButton` (matching `player_page.cc` needs)
- No breaking changes — existing `IconButton` vector paths remain intact

## Capabilities

### New Capabilities
- `app-icons`: Unified icon resource management — a `.qrc` file compiling all SVG icons into the binary as `:/icons/*.svg`, loadable via `QIcon` or `QSvgRenderer`

### Modified Capabilities
- `app-shell-ui`: Navigation bar items gain icons via `NavItem::SetIcon()`. Home dashboard cards use SVG-backed thumbnails instead of custom `QPainter` lambdas.

## Impact

- **`src/app/CMakeLists.txt`**: Add `find_package(Qt6 REQUIRED COMPONENTS Svg)` and link `Qt6::Svg`; add `qt_add_resources()` for the `.qrc` file
- **`src/app/resources/icons/icons.qrc`** (new): Lists all SVG icons
- **`src/app/navigation_bar.cc`**: 3 calls to `SetIcon()` with resource URLs
- **`src/app/home_page.cc`**: Replace `PaintPlayGlyph` / `PaintConvertGlyph` lambdas with `QIcon`-based thumbnail painting
- **`src/app/app_theme.h`**: May add a helper constant for the icon resource path prefix
