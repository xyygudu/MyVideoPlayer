## ADDED Requirements

### Requirement: Icon resource compilation
A `.qrc` resource file SHALL compile all SVG icons from `src/app/resources/icons/` into the application binary, making them accessible via `:/icons/<filename>.svg` resource paths. The `Qt6::Svg` module SHALL be linked to enable SVG rendering via `QIcon`.

#### Scenario: SVG icon loads from resource
- **WHEN** the app creates `QIcon(":/icons/home.svg")`
- **THEN** the icon renders correctly without file-not-found errors or runtime SVG parsing failures

### Requirement: Nav bar icon assignment
Three navigation items SHALL have icons assigned via `NavItem::SetIcon()`:
- "主页" SHALL use `:/icons/home.svg`
- "播放器" SHALL use `:/icons/player.svg`
- "转码器" SHALL use `:/icons/converter.svg`

The 18×18px reserved icon slot and 8px icon-text gap (defined in `app_theme.h`) SHALL remain unchanged.

#### Scenario: Nav bar shows icons on launch
- **WHEN** the app starts and the navigation bar is rendered
- **THEN** each of the three nav items shows its corresponding SVG icon in the 18×18px slot, left-aligned at `kNavItemLeftPadding` with the text starting at `icon_left + 18 + 8` pixels

### Requirement: Home dashboard card thumbnails use SVG icons
The two `DashboardCard` instances on the Home page SHALL replace their custom `QPainter` thumbnail lambdas (`PaintPlayGlyph`, `PaintConvertGlyph`) with `QIcon`-backed rendering using the SVG resource paths `:/icons/player.svg` and `:/icons/converter.svg` respectively. The thumbnail SHALL be rendered as a centered 48×48px icon on the existing `kAccentSoft` background.

#### Scenario: Home card shows SVG thumbnail
- **WHEN** the Home page is displayed
- **THEN** the "播放器" card thumbnail shows the player SVG icon centered on a `kAccentSoft` background, and the "转码器" card thumbnail shows the converter SVG icon on the same background
