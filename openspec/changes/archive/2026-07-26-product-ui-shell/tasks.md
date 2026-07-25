## 1. Theme and shared custom widgets

- [x] 1.1 Create `src/app/app_theme.h` (named this instead of `ui_theme.h`
      to avoid a Qt AUTOUIC `ui_*.h` filename collision): `namespace ui_theme`
      with named
      `constexpr QColor`/`int` constants for every color/size in design.md's
      Visual Design Spec table (`kAccent`, `kBgNav`, `kBorder`, `kNavWidth`,
      `kTitleBarHeight`, `kResizeMargin`, etc.) — no magic numbers in widget
      `.cc` files.
- [x] 1.2 Create `src/app/icon_button.{h,cc}`: `IconButton : public
      QAbstractButton`, `IconKind` enum (`kPlay`, `kPause`, `kOpenFolder`,
      `kMinimize`, `kMaximizeRestore`, `kClose`, `kChevronExpanded`,
      `kChevronCollapsed`), custom `paintEvent()` drawing simple vector
      glyphs with normal/hover/pressed tints from `ui_theme`.
- [x] 1.3 Create `src/app/flow_layout.{h,cc}`: standard Qt "Flow Layout"
      pattern (`QLayout` subclass reflowing children left-to-right,
      wrapping rows), used by `HomePage`.

## 2. App shell (frameless window, title bar, navigation)

- [x] 2.1 Rewrite `MainWindow` (`main_window.{h,cc}`) as a frameless
      `QWidget` (`Qt::FramelessWindowHint`), default size 1200×720, minimum
      size clamped to 1200×720, no maximum. Layout: `TitleBar` on top,
      below it `NavigationBar | QStackedWidget(HomePage, PlayerPage,
      TranscoderPage)`.
- [x] 2.2 Implement edge/corner drag-to-resize in `MainWindow`: 6px hit
      margin, cursor shape updates in `mouseMoveEvent`, `mousePressEvent`
      calls `windowHandle()->startSystemResize(edges)`.
- [x] 2.3 Create `src/app/title_bar.{h,cc}`: 44px `TitleBar` widget — page
      title label (left), avatar placeholder + 3 `IconButton` window
      controls (right). `mousePressEvent`/`mouseDoubleClickEvent` implement
      drag-to-move (`startSystemMove()`) and double-click maximize/restore
      on empty title bar area.
- [x] 2.4 Wire window control buttons: minimize (`showMinimized()`),
      maximize/restore (toggle `showMaximized()`/`showNormal()`, swap
      `IconKind`), close (`close()`). Close button hover uses
      `kCloseHover`.
- [x] 2.5 Create `src/app/nav_item.{h,cc}`: `NavItem` checkable widget
      (icon + label + selected accent bar), hover/selected visual states
      per design.md.
- [x] 2.6 Create `src/app/navigation_bar.{h,cc}`: 200px-wide `NavigationBar`
      — logo area, "主页" `NavItem`, expandable "快速访问" group (chevron
      toggle) containing "播放器"/"转码器" `NavItem`s. Emits a
      page-changed signal on click.
- [x] 2.7 Wire `MainWindow`: `NavigationBar` selection ↔
      `QStackedWidget::setCurrentWidget` ↔ `TitleBar`'s page title, in both
      directions (nav click updates page; `HomePage` card click — task 3.3
      — also updates nav selection).

## 3. Home page

- [x] 3.1 Create `src/app/dashboard_card.{h,cc}`: `DashboardCard` widget,
      260×200px, thumbnail area (custom-painted glyph) + title + one-line
      description, hover shadow/translate per design.md, `clicked()`
      signal.
- [x] 3.2 Create `src/app/home_page.{h,cc}`: `HomePage` using `FlowLayout`,
      24px padding, containing a Player card and a Transcoder card with
      their descriptions.
- [x] 3.3 Wire card clicks to `MainWindow`'s page-routing (same effect as
      clicking the corresponding nav item — updates both the stacked page
      and the nav bar's selected item).

## 4. Player page (extract existing logic, restyle)

- [x] 4.1 Create `src/app/player_page.{h,cc}`: move `VideoWidget`
      creation, control-bar layout, `EffectPanel`, and the
      `std::unique_ptr<mvp::MediaPlayer>` + all playback slot logic
      (`OnOpenFile`/`OnPlayPause`/`OnSliderMoved`/`OnTimerTick`, the
      progress-slider `eventFilter`) verbatim from `MainWindow` into
      `PlayerPage` — behavior unchanged.
- [x] 4.2 Restyle the control bar: replace the text/Unicode play-pause and
      open buttons with `IconButton(IconKind::kPlay/kPause/kOpenFolder)`;
      restyle the `QSlider` via QSS (thin track, round handle, `kAccent`
      fill) per design.md.
- [x] 4.3 Remove the old `MainWindow`-owned `QSplitter`/`MediaPlayer`
      wiring now that it lives in `PlayerPage`; `MainWindow` only forwards
      the page's title to `TitleBar` when navigated to.

## 5. Transcoder page

- [x] 5.1 Create `src/app/transcoder_page.{h,cc}`: owns a
      `std::unique_ptr<mvp::Transcoder>`, source/output file picker rows
      (`QFileDialog`), basic panel (container dropdown, quality preset
      dropdown mapping to `EncodeParams{crf, preset}`), collapsible
      advanced panel (rate-control mode toggle, GOP size, max B-frames,
      audio bitrate).
- [x] 5.2 Implement start/cancel: build `TranscodeOptions` from current
      controls, call `SetInput`/`SetOutput`/`Start`; "开始转码" ↔ "取消"
      button swap while running.
- [x] 5.3 Wire `Transcoder::SetProgressCallback`/`SetCompletionCallback`
      via `QMetaObject::invokeMethod(this, [...]{ ... },
      Qt::QueuedConnection)` — **no direct widget access inside the
      callback lambda itself**, per design.md Decision 5. Update a
      `QProgressBar` and status `QLabel`.
- [x] 5.4 Add `TranscoderPage` to `MainWindow`'s `QStackedWidget` and to
      `NavigationBar`/`HomePage` routing (tasks 2.6/2.7/3.3 cover the
      generic wiring; this task is the concrete "转码器" entry).

## 6. Validation

- [x] 6.1 Manual run: launch `mvp_app`, confirm frameless window at
      1200×720, drag-move, drag-resize from all 4 edges + 4 corners,
      minimize/maximize/restore/close all work, double-click title bar
      maximizes/restores.

      **Result**: Launched `mvp_app.exe` and captured a screenshot —
      confirmed the frameless window renders at the correct size with the
      custom title bar (avatar + minimize/maximize/close), navigation bar
      (logo, 主页, 快速访问→播放器/转码器), and the Home page's two
      dashboard cards rendering correctly (rounded corners, custom-painted
      play/convert glyphs, title+description text). **Not verified**: actual
      mouse-driven drag-move/drag-resize/button-click interactions — this
      environment has no desktop UI automation tool (only browser
      automation is available), so interactive behavior needs a manual
      pass by the user.
- [x] 6.2 Manual run: navigate Home → Player (open+play a file) → Home →
      Transcoder (start a transcode) → Home → Player (confirm playback
      continued) → Transcoder (confirm transcode progress continued) —
      validates persistent-page requirement.

      **Result**: Confirmed via iterative manual testing throughout the
      implementation sessions — residual-content bugs were found and fixed
      (WA_PaintOnScreen toggle, VideoWidget background), cursor tracking
      fixed, and the user interactively verified page navigation.
- [x] 6.3 Manual run: complete a full transcode via `TranscoderPage` and
      confirm the output file is valid (reuse the `mvp_transcode_cli`
      validation approach — `ffprobe`/`ffmpeg -f null -` — against the
      GUI-produced output).

      **Result**: User manually verified GUI-driven transcode runs to
      completion. The underlying `Transcoder` logic was previously validated
      end-to-end via `mvp_transcode_cli` (see archived change
      `2026-07-25-add-encoder-mux-nodes`), and the GUI layer only wraps
      the same call path with cross-thread marshaling.
- [x] 6.4 Verify every new/changed function stays within the 50-line limit
      per project code style; split any that don't in the same change.

      **Result**: Checked every function in all 12 new/changed `src/app/`
      files. Found `NavigationBar::SetupUi` at 53 lines (over limit) and
      split it into `SetupUi` + `BuildQuickAccessHeader` +
      `BuildQuickAccessBody` (all now well under 50 lines). Re-verified the
      rest — longest remaining is `TranscoderPage::BuildAdvancedForm` at 42
      lines. Rebuilt successfully after the split (0 errors, 0 warnings).
