## Purpose

Defines the frameless application shell: custom title bar (drag-to-move,
drag-to-resize, window control buttons), the left navigation bar, and the
page-routing container that hosts the Home/Player/Transcoder pages.

## Requirements

### Requirement: Frameless top-level window
`MainWindow` SHALL be a top-level `QWidget` with `Qt::FramelessWindowHint`
(no native OS title bar/border). Default size SHALL be 1200×720; minimum
size SHALL be clamped to 1200×720 with no maximum, so the window can be
resized arbitrarily larger or maximized.

#### Scenario: App launches at default size
- **WHEN** `mvp_app` starts
- **THEN** the window shows at 1200×720 with no native title bar

#### Scenario: Window cannot shrink below minimum
- **WHEN** the user drags an edge/corner to shrink the window
- **THEN** the window stops shrinking at 1200×720

### Requirement: Drag-to-move via native system move
Clicking and dragging the title bar (outside its buttons/avatar) SHALL move
the window by invoking `QWindow::startSystemMove()` — no manual mouse-delta
tracking.

#### Scenario: Drag title bar moves the window
- **WHEN** the user presses and drags on an empty area of the title bar
- **THEN** the window follows the cursor via the OS-native move loop

### Requirement: Drag-to-resize via native system resize
`MainWindow` SHALL detect the mouse being within a 6px margin of any edge or
corner and, on press, invoke `QWindow::startSystemResize(Qt::Edges)` with
the corresponding edge combination. The cursor shape SHALL change to the
matching resize cursor while hovering that margin.

#### Scenario: Drag a corner resizes both dimensions
- **WHEN** the user presses and drags within 6px of the bottom-right corner
- **THEN** both width and height resize following the cursor, cursor shape
  is `Qt::SizeFDiagCursor`

#### Scenario: Drag an edge resizes one dimension
- **WHEN** the user presses and drags within 6px of the left edge
- **THEN** only the width resizes, cursor shape is `Qt::SizeHorCursor`

### Requirement: Title bar content and window controls
The title bar SHALL be 40px tall, spanning the window width minus the
navigation bar. It SHALL show the current page title on the left, and on
the right: a 24px circular avatar placeholder (click is a no-op) and three
40×40px window control buttons (minimize, maximize/restore, close), each
custom-painted via `IconButton`. (Dimensions were reduced from the original
44/44/28 after user feedback that the original buttons looked oversized.)

#### Scenario: Close button hover
- **WHEN** the cursor hovers the close button
- **THEN** its background becomes `kCloseHover` (`#E81123`) with a white
  glyph; the other two buttons use `kHoverTint` on hover

#### Scenario: Double-click title bar toggles maximize
- **WHEN** the user double-clicks an empty area of the title bar
- **THEN** the window toggles between maximized and its previous size

#### Scenario: Window control buttons perform their action
- **WHEN** the user clicks minimize / maximize-restore / close
- **THEN** the window minimizes / toggles maximized-restored / closes the
  application respectively

### Requirement: Left navigation bar
The navigation bar SHALL be a 200px-wide fixed-width column spanning the
full window height, background `kBgNav`. It SHALL contain a 56px logo area
(app icon + name) followed by a vertical menu: "主页" (Home), and an
expandable "快速访问" group (default expanded) containing "播放器" and
"转码器" sub-items. Each item is 40px tall.

#### Scenario: Selected item visual state
- **WHEN** a nav item corresponds to the currently displayed page
- **THEN** it shows a 3px `kAccent` left bar, `kAccentSoft` background, and
  `kAccent` text color

#### Scenario: Hover visual state
- **WHEN** the cursor hovers a non-selected nav item
- **THEN** its background becomes `kHoverTint`

#### Scenario: Collapsing the quick-access group hides its sub-items
- **WHEN** the user clicks the "快速访问" group header
- **THEN** "播放器"/"转码器" sub-items are hidden (and shown again on next
  click), the group's chevron glyph reflects the current state

### Requirement: Page routing via persistent stacked pages
The content area SHALL be a `QStackedWidget` holding exactly three page
widgets — Home, Player, Transcoder — all constructed once at startup and
never destroyed while `MainWindow` is alive. Navigation SHALL only call
`QStackedWidget::setCurrentWidget`, never recreate a page.

#### Scenario: Switching pages preserves state
- **WHEN** the user starts a transcode on the Transcoder page, then
  navigates to Home, then back to Transcoder
- **THEN** the transcode is still running and progress reflects elapsed
  time (the page/`Transcoder` instance was never destroyed)

#### Scenario: Clicking a nav item switches the visible page
- **WHEN** the user clicks "播放器" in the navigation bar
- **THEN** the content area shows `PlayerPage` and the title bar's page
  title updates to "播放器"
