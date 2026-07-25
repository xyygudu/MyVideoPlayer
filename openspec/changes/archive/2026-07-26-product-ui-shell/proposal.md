## Why

`mvp_app` currently opens straight into a bare video player window (native OS
title bar, plain buttons, no navigation). With `Transcoder` now implemented in
`mvp_core` (see `openspec/changes/archive/2026-07-25-add-encoder-mux-nodes/`),
the app needs a real UI entry point for it — and this is a good opportunity to
restructure the whole shell into a small multi-tool product (home dashboard +
player + transcoder) with a custom frameless window, both for a better user
experience and as a learning exercise in custom Qt widget construction.

## What Changes

- **BREAKING (internal only)**: `MainWindow` changes from a `QMainWindow` with
  native title bar + `QSplitter(video | EffectPanel)` to a frameless
  `QWidget` shell composed of a custom title bar, a left navigation bar, and a
  `QStackedWidget` hosting three persistent pages: Home, Player, Transcoder.
- Add a custom title bar (drag-to-move via `startSystemMove()`, double-click
  to maximize/restore, custom-painted minimize/maximize/close buttons, a
  static avatar placeholder).
- Add a left navigation bar (logo, "主页" item, expandable "快速访问" group
  with "播放器"/"转码器" sub-items), with selected/hover visual states.
- Add a Home dashboard page with a responsive card grid (custom `FlowLayout`)
  — one card per tool (Player, Transcoder), custom-painted thumbnail icons,
  hover feedback, click-to-navigate.
- Extract the existing video canvas + control bar + `EffectPanel` (currently
  inline in `MainWindow`) into a new `PlayerPage`, restyled with custom
  icon-painted buttons and a restyled progress slider — behavior unchanged.
- Add a new `TranscoderPage` wired to `mvp::Transcoder`/`mvp::TranscodeOptions`
  — covers only the capabilities `Transcoder` currently implements (container
  choice, quality preset, basic + advanced encode parameters, progress,
  start/cancel). No UI entry points for unimplemented features (trim,
  passthrough, two-pass, hardware encode, filters/scaling).
- Add a small reusable custom-widget library: `IconButton` (vector-painted
  icons, no external image assets), `NavItem`, `DashboardCard`, `FlowLayout`.
- Add a single app-wide light theme (QSS + a `app_theme.h` constants header for
  colors/spacing, avoiding magic numbers scattered across widgets).
- Window is fixed at 1200×720 (min size == default size); only resize
  interaction is maximize/restore — no edge-drag resizing.

## Capabilities

### New Capabilities
- `app-shell-ui`: frameless window chrome (custom title bar, window controls,
  drag-to-move), left navigation bar, and page-routing shell that hosts Home/
  Player/Transcoder pages.
- `home-dashboard-ui`: card-grid home page, one card per tool, click-to-
  navigate.
- `transcoder-ui`: transcode page wired to `mvp::Transcoder` — file pickers,
  quality preset + advanced parameters, progress reporting, start/cancel,
  with explicit cross-thread marshaling since `Transcoder` callbacks fire off
  the GUI thread.

### Modified Capabilities
- `player-ui`: existing requirements (video display, play/pause, progress
  slider, time display, open file, EffectPanel hosting) are relocated from
  `MainWindow` into a new `PlayerPage` widget and restyled (icon buttons,
  restyled slider). Playback behavior/requirements themselves are unchanged;
  only the hosting widget and visual styling change.

## Impact

- `src/app/main_window.{h,cc}`: rewritten as the frameless shell (title bar +
  nav bar + `QStackedWidget`), no longer owns `mvp::MediaPlayer` directly.
- New files: `src/app/title_bar.{h,cc}`, `src/app/navigation_bar.{h,cc}`,
  `src/app/nav_item.{h,cc}`, `src/app/home_page.{h,cc}`,
  `src/app/dashboard_card.{h,cc}`, `src/app/flow_layout.{h,cc}`,
  `src/app/player_page.{h,cc}`, `src/app/transcoder_page.{h,cc}`,
  `src/app/icon_button.{h,cc}`, `src/app/app_theme.h`.
- `src/app/video_widget.{h,cc}` and `src/app/effect_panel.{h,cc}` are reused
  unchanged, just re-parented into `PlayerPage`.
- No changes to `mvp_core`'s public API — `TranscoderPage` consumes the
  existing `mvp::Transcoder`/`mvp::TranscodeOptions` as-is.
- No new third-party dependencies (custom widgets painted with `QPainter`,
  no icon font / image asset library).
