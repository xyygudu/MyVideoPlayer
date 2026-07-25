## Context

`mvp_app` today: `MainWindow : QMainWindow` with the native OS title bar, a
`QSplitter(video+controls | EffectPanel)` as central widget. `mvp::MediaPlayer`
is owned directly by `MainWindow`. `mvp::Transcoder` (from
`openspec/changes/archive/2026-07-25-add-encoder-mux-nodes/`) has no GUI yet —
only a CLI smoke-test tool.

This change turns the app into a small "toolbox" shell: frameless window,
custom title bar, left navigation, and three pages (Home / Player /
Transcoder) behind a `QStackedWidget`. This is also the visual design spec
requested — dimensions/colors/states below are normative for implementation.

Two scope questions were asked. Window resize behavior has been confirmed:
**arbitrary edge/corner drag-resize is supported, minimum size 1200×720, no
Aero Snap edge-docking (dragging to a screen edge to auto half-dock)**.
Transcoder page scope defaulted to the recommended option while the user was
unavailable — **please confirm/override** (see Open Questions).

## Goals / Non-Goals

**Goals:**
- Ship a cohesive, custom-drawn shell (title bar, nav, cards, icon buttons)
  using only `QPainter` — no external icon/image asset dependency.
- Encapsulate each visual concern as its own reusable widget class (per
  project's anti-patch-coding rule: don't stack one-off painting logic in
  `MainWindow`).
- Wire a real `TranscoderPage` against the existing `Transcoder` API,
  correctly handling its cross-thread callbacks.
- Preserve all existing `PlayerPage` behavior/requirements (only the hosting
  widget and visuals change — see `player-ui` delta spec).

**Non-Goals:**
- Dark theme / theme switching (light theme only for now; theme constants
  are centralized so adding one later is a small change, not a redesign).
- Aero Snap edge-docking (dragging to a screen edge to auto-dock/half-size)
  and the Windows 11 "Snap Layouts" hover flyout on the maximize button.
  Arbitrary resize itself IS in scope (see Decision 2).
- Any UI for `Transcoder` features not yet implemented (trim, passthrough,
  two-pass, hardware encode, resolution/fps override via `AVFilterNode`).
- Real user-account system behind the avatar (static placeholder only).

## Decisions

### 1. Frameless shell is a `QWidget`, not `QMainWindow`
`QMainWindow` exists to host a native menu/tool/status bar chrome we no
longer want. Switching the top-level `MainWindow` to a plain frameless
`QWidget` (`Qt::FramelessWindowHint | Qt::Window`) with a manual
`QVBoxLayout(TitleBar, QHBoxLayout(NavigationBar, QStackedWidget))` is
simpler than fighting `QMainWindow`'s built-in chrome assumptions.

### 2. Drag-to-move/resize via `startSystemMove()`/`startSystemResize()`, no manual delta tracking or WM_NCHITTEST
**Alternative considered**: track `mousePressEvent`/`mouseMoveEvent` deltas
and call `move()`/`resize()` manually, or intercept `WM_NCHITTEST` in a
`nativeEvent()` override to report `HTLEFT`/`HTTOPLEFT`/etc. **Rejected**:
Qt 6's `QWindow::startSystemMove()` and `QWindow::startSystemResize(Qt::Edges)`
hand the gesture to the OS's native move/resize loop directly — same result
as a manual `WM_NCHITTEST` implementation with far less code and no manual
multi-monitor/DPI edge cases to handle.

- **Move**: `TitleBar::mousePressEvent` calls
  `window()->windowHandle()->startSystemMove()`.
- **Resize**: `MainWindow` defines a `kResizeMargin = 6px` border strip. In
  `mouseMoveEvent`, if the cursor is within that margin, set the matching
  `Qt::SizeFDiagCursor`/`SizeBDiagCursor`/`SizeHorCursor`/`SizeVerCursor`;
  on `mousePressEvent` in that margin, call
  `windowHandle()->startSystemResize(edges)` with the detected
  `Qt::Edge` combination (e.g. `Qt::LeftEdge | Qt::TopEdge` for the
  top-left corner).
- `setMinimumSize(1200, 720)` enforces the floor; no `setMaximumSize` is
  set, so the window can grow arbitrarily large (and still maximizes
  normally). Using the native move/resize loop means Windows' own
  drag-to-edge-while-moving/Win+Arrow snapping may still occur as a side
  effect of the OS loop — that's fine (no extra work either way); what is
  explicitly out of scope is the Windows 11 "Snap Layouts" flyout that
  appears when hovering the maximize button, which requires additional
  native integration we are not building.

### 3. Persistent pages in `QStackedWidget`, not recreated per navigation
Home/Player/Transcoder pages are constructed once in `MainWindow`'s
constructor and only shown/hidden via `QStackedWidget::setCurrentWidget`.
**Why**: `TranscoderPage` owns a `mvp::Transcoder` instance — if the page
were destroyed when navigating away, an in-progress transcode would be
killed. Persistent pages let a transcode keep running in the background
while the user browses Home/Player, matching mainstream app behavior
(browsers keep tabs alive, IDEs keep background tasks running).

### 4. Custom-painted `IconButton` instead of image/icon-font assets
A single reusable `IconButton : public QAbstractButton` takes an
`IconKind` enum (`kPlay`, `kPause`, `kOpenFolder`, `kMinimize`,
`kMaximizeRestore`, `kClose`, `kChevronExpanded`, `kChevronCollapsed`, ...)
and paints a simple vector glyph in `paintEvent()` (triangles, rounded
rects, lines) using `Theme` colors, with distinct normal/hover/pressed
tints. **Why over an icon font or PNG assets**: zero binary assets to
manage/license, trivially recolorable per theme, and it is explicitly a
"custom widget" exercise the user asked for. **Trade-off**: glyphs are
simple geometric shapes, not polished icon-designer artwork — acceptable
for a learning project; documented as a place to swap in real assets later
without changing the call sites (`IconButton(IconKind::kPlay)`).

### 5. `TranscoderPage` cross-thread callback marshaling
`Transcoder::SetProgressCallback`/`SetCompletionCallback` fire from the
graph's internal worker threads (the `MuxNode` fan-in thread — see
`mux_node.cc`/`transcoder.cc`), **not** the Qt GUI thread. `TranscoderPage`
must never touch a `QWidget` directly inside those callback lambdas.

**Decision**: the callbacks call `QMetaObject::invokeMethod(this, [...]{ ...
widget updates... }, Qt::QueuedConnection)`, which safely hops the update
onto the GUI thread's event loop. This reuses Qt's existing queued-connection
mechanism (the same mechanism Qt itself uses for cross-thread signals) rather
than introducing a new synchronization primitive — consistent with the
project's "reuse existing mechanisms" rule.

### 6. Theme constants centralized in `app_theme.h`
All colors/spacing/sizes live in one header (`namespace ui_theme { inline
constexpr QColor kAccent{...}; inline constexpr int kNavWidth = 200; ... }`)
instead of being scattered as magic numbers across widget `.cc` files, per
project code-style rule (avoid magic numbers; inject via config). The file
is named `app_theme.h` (not `ui_theme.h`) because Qt's AUTOUIC build step
treats any `#include "ui_*.h"` as a request for a generated Designer-form
header and fails the build looking for a nonexistent `.ui` file — the
`ui_theme` C++ namespace name is unaffected, only the file itself needed a
different name.

### 7. `FlowLayout` reused from the standard Qt example pattern
A minimal custom `QLayout` subclass (the well-known Qt "Flow Layout"
example shape: reflows children left-to-right, wrapping to the next row) is
implemented locally in `src/app/flow_layout.{h,cc}` — no new dependency,
small and self-contained, gives the card grid the "reflows as window widens"
behavior requested. This matters more now that the window supports
arbitrary resize (Decision 2): the grid must reflow continuously as the
user drags the window wider/narrower, not just at two fixed sizes.

## Visual Design Spec

**Window**: 1200×720 default size, frameless, no native chrome. Resizable by
dragging any edge/corner (6px hit margin) or via maximize/restore; minimum
size clamped to 1200×720, no maximum.

**Palette (light theme)**:
| Token | Hex | Usage |
|---|---|---|
| `kBgNav` | `#F5F6F8` | Navigation bar background |
| `kBgContent` | `#FFFFFF` | Content area / title bar background |
| `kAccent` | `#4F7CFF` | Selected nav item, active accents, slider fill |
| `kAccentSoft` | `#EAF0FF` | Selected nav item background tint |
| `kHoverTint` | `#EFEFF2` | Generic hover background |
| `kTextPrimary` | `#1F2329` | Titles, primary labels |
| `kTextSecondary` | `#767C88` | Descriptions, secondary labels |
| `kBorder` | `#E5E6EB` | Card/divider borders |
| `kCloseHover` | `#E81123` | Close button hover background |

**Layout dimensions**:
- Nav bar: 200px fixed width, full window height.
  - Logo area: 56px height.
  - Nav item: 40px height, 12px left padding, 3px accent bar when selected.
- Title bar: 44px height, spans width minus nav bar.
  - Right: 28px circular avatar placeholder + 3× 44×44px window buttons.
- Content padding: 24px on Home/Transcoder pages.
- Dashboard card: 260×200px (160px thumbnail + ~40px text), 8px corner
  radius, 1px `kBorder`, hover: shadow deepens + 2px upward translate.

**States**: nav item hover = `kHoverTint` background; selected = `kAccentSoft`
background + `kAccent` left bar + `kAccent` text. Card hover = shadow
`0 2 8 rgba(0,0,0,0.06)` → `0 6 16 rgba(0,0,0,0.14)`. Window close button
hover = `kCloseHover` background + white glyph; minimize/maximize hover =
`kHoverTint`.

## Risks / Trade-offs

- **[Risk]** Custom-painted icons look plainer than a professional icon set
  → **Mitigation**: acceptable for a learning project; `IconKind` enum keeps
  the door open to swap in SVG assets later without touching call sites.
- **[Risk]** Rewriting `MainWindow` from `QMainWindow` to a frameless
  `QWidget` touches the app's entire top-level structure → **Mitigation**:
  `VideoWidget`/`EffectPanel`/`mvp::MediaPlayer` wiring logic is moved
  verbatim into `PlayerPage`, not rewritten — behavior risk is limited to
  the new chrome/navigation code, not existing playback logic.
- **[Risk]** Forgetting `Qt::QueuedConnection` when wiring `Transcoder`
  callbacks would crash or corrupt widget state (cross-thread UI access)
  → **Mitigation**: called out explicitly as a hard rule in Decision 5 and
  in `tasks.md` for whoever implements `TranscoderPage`.

## Migration Plan

Purely additive/restructuring within `src/app/` — no `mvp_core` API changes,
no persisted user data/format to migrate. Rollback = revert the commit.

## Open Questions

None outstanding — window resize behavior was confirmed by the user
(arbitrary drag-resize, no Aero Snap). The remaining two questions
(Transcoder page scope; avatar click behavior) were re-asked with the user
unavailable to respond, so the recommended defaults stand: Transcoder page
covers only implemented capabilities (no disabled placeholders), and the
avatar placeholder is a no-op on click. Both are low-impact/reversible and
can be revisited after review.
