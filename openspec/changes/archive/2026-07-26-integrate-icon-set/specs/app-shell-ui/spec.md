## ADDED Requirements

### Requirement: Navigation item icons
Each navigation item in the left bar SHALL display a 18×18px SVG icon in the reserved icon slot, loaded from the application's Qt resource system at `:/icons/<name>.svg`. The icon SHALL be rendered at `kNavItemLeftPadding` (12px) from the left edge, with the text label starting at `icon_left + kNavIconSize + kNavIconTextGap` (38px) to maintain alignment with the existing layout.

#### Scenario: Home nav item shows home icon
- **WHEN** the navigation bar is rendered
- **THEN** the "主页" item shows the home SVG icon in its 18×18px slot, tinted with `kAccent` when selected and `kTextPrimary` otherwise

#### Scenario: Player nav item shows player icon
- **WHEN** the navigation bar is rendered
- **THEN** the "播放器" item shows the player SVG icon in its 18×18px slot

#### Scenario: Transcoder nav item shows converter icon
- **WHEN** the navigation bar is rendered
- **THEN** the "转码器" item shows the converter SVG icon in its 18×18px slot

### Requirement: Home card thumbnail icons
The two `DashboardCard` widgets on the Home page SHALL use SVG icons as their thumbnail content, rendered at 48×48px centered on the `kAccentSoft` background, replacing the custom `QPainter` lambdas.

#### Scenario: Player card shows player SVG thumbnail
- **WHEN** the Home page displays the "播放器" card
- **THEN** the thumbnail area shows the player SVG icon centered on a `kAccentSoft` rounded rectangle background

#### Scenario: Transcoder card shows converter SVG thumbnail
- **WHEN** the Home page displays the "转码器" card
- **THEN** the thumbnail area shows the converter SVG icon centered on a `kAccentSoft` rounded rectangle background
