## Purpose

Defines the Home page: a responsive card grid with one card per tool
(Player, Transcoder), used to navigate into that tool.
## Requirements
### Requirement: Responsive card grid
`HomePage` SHALL lay out its cards using a custom `FlowLayout` with 24px
content padding, so cards reflow (wrap to the next row) as the window is
resized, rather than using a fixed-column grid.

#### Scenario: Cards reflow on window resize
- **WHEN** the window is resized narrower such that two cards no longer fit
  on one row
- **THEN** the second card wraps to the next row without any code change
  to card count/order

### Requirement: Dashboard card content and dimensions
Each `DashboardCard` SHALL be 280×240px with an 8px corner radius, 1px
`kBorder` border, white background: a 140px-tall thumbnail area on top
filled by a cover image (`.png` from `:/images/`), and a ~60px text area
below showing a title and a one-line description. The cover image SHALL
be scaled to fill the thumbnail area while preserving aspect ratio, with
overflow cropped from center (equivalent to CSS `object-fit: cover`).

#### Scenario: Player card content
- **WHEN** the Home page is shown
- **THEN** one card reads "播放器" with a short description of the
  playback feature, and its thumbnail shows `player_cover.png`
  center-cropped to fill the 280×140px thumbnail area

#### Scenario: Transcoder card content
- **WHEN** the Home page is shown
- **THEN** one card reads "转码器" with a short description of the
  transcode feature, and its thumbnail shows `trancoder_cover.png`
  center-cropped to fill the 280×140px thumbnail area

### Requirement: Card hover and click behavior
A `DashboardCard` SHALL deepen its shadow and shift 2px upward on hover.
Clicking a card SHALL navigate `MainWindow` to that tool's page and update
the navigation bar's selected item to match.

#### Scenario: Hover feedback
- **WHEN** the cursor enters a card's bounds
- **THEN** its shadow changes from `0 2 8 rgba(0,0,0,0.06)` to
  `0 6 16 rgba(0,0,0,0.14)` and it translates 2px upward

#### Scenario: Click navigates to the tool
- **WHEN** the user clicks the "转码器" card
- **THEN** the content area switches to `TranscoderPage` and the "转码器"
  nav item becomes selected, identical to clicking the nav item directly

### Requirement: 主页提供录屏卡片
HomePage SHALL 在播放器、转码器卡片之后提供「录屏」卡片：缩略图为代码绘制的强调色显示器与红色录制圆点（不依赖图片资源），描述为「录制全屏、窗口或选定区域，可同时录制系统声音、麦克风与摄像头画面。」点击 SHALL 发出 `NavigateRequested(Page::kRecorder)`。

#### Scenario: 点击录屏卡片
- **WHEN** 在主页点击「录屏」卡片
- **THEN** 切换到录屏页，导航栏「录屏」项变为选中态

