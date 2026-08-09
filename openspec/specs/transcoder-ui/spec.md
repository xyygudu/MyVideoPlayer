## Purpose

Defines the Transcoder page: file pickers, quality/parameter controls wired
to `mvp::TranscodeOptions`, and progress/completion reporting from
`mvp::Transcoder`, scoped to only the capabilities `Transcoder` currently
implements (no trim, passthrough, two-pass, or hardware encode entry
points).

## Requirements

### Requirement: Source and output file selection
`TranscoderPage` SHALL provide a source file row (path display + "浏览"
button opening `QFileDialog::getOpenFileName`) and an output file row (path
display + "另存为" button opening `QFileDialog::getSaveFileName`,
defaulting to the source's directory with a "_transcoded" suffix and the
container extension chosen below).

#### Scenario: Selecting a source file populates the path
- **WHEN** the user picks a file via the source "浏览" dialog
- **THEN** the source path field updates and the "开始转码" button becomes
  enabled (previously disabled with no source selected)

### Requirement: Basic parameter panel maps to TranscodeOptions
The always-visible basic panel SHALL offer: a container/format dropdown
(mp4, mkv — passed through as the output path's extension), an encoder
dropdown per media type (video/audio) populated from the selected
container's `ContainerCodecCaps` (see `container-codec-caps` capability),
and a quality preset dropdown ("高质量" / "均衡" / "快速") that maps to a
fixed `{crf, preset}` pair per option (e.g. 高质量→crf 18/slow, 均衡→crf
23/medium, 快速→crf 28/fast).

The encoder dropdowns SHALL default to the selected container's default
codec (`ContainerCodecCaps::default_video_codec` /
`default_audio_codec`). Changing the container dropdown SHALL refresh both
encoder dropdowns' available options and, if the currently selected
encoder is no longer valid for the new container, reset the selection to
the new container's default — a stale, no-longer-valid selection SHALL
NOT be silently kept.

#### Scenario: Selecting a quality preset sets crf/preset
- **WHEN** the user selects "高质量"
- **THEN** the resulting `TranscodeOptions.video.crf == 18` and
  `preset == "slow"`

#### Scenario: Changing container refreshes encoder options
- **WHEN** the user switches the container dropdown from "mp4" to "mkv"
- **THEN** both encoder dropdowns repopulate from mkv's `ContainerCodecCaps`

#### Scenario: Switching container resets an invalid encoder selection
- **WHEN** the currently selected video encoder is not present in the new
  container's supported codec list
- **THEN** the video encoder dropdown resets to the new container's
  default video codec

#### Scenario: Default encoder selected on first load
- **WHEN** `TranscoderPage` is first shown with the default container
- **THEN** both encoder dropdowns are pre-selected to that container's
  default video/audio codec

### Requirement: Advanced parameter panel (collapsed by default)
An "高级设置" collapsible section SHALL expose: rate-control mode (CRF vs
target bitrate) with the corresponding numeric field, GOP size, max B-frame
count, and audio bitrate — all mapping directly to `EncodeParams` fields.
No controls for trim range, stream passthrough/copy, two-pass encoding, or
hardware encode SHALL be present, matching `Transcoder`'s current
implementation scope.

#### Scenario: Advanced panel starts collapsed
- **WHEN** `TranscoderPage` is first shown
- **THEN** the advanced section is collapsed; basic panel is fully visible

#### Scenario: Switching rate-control mode changes the visible numeric field
- **WHEN** the user switches from "CRF" to "目标码率"
- **THEN** the CRF spin box is hidden and a bitrate (kbps) spin box is shown

### Requirement: Start/cancel and progress reporting
`TranscoderPage` SHALL provide a "开始转码" button that constructs
`TranscodeOptions` from the current controls and calls
`Transcoder::SetInput`/`SetOutput`/`Start`, then a progress bar (0-100) and
status label. While running, the start button SHALL be replaced by a
"取消" button calling `Transcoder::Cancel()`.

#### Scenario: Progress bar updates during transcode
- **WHEN** `Transcoder`'s progress callback reports 42.0
- **THEN** the progress bar shows 42% (see cross-thread requirement below)

#### Scenario: Completion shows success or failure status
- **WHEN** the completion callback reports `true`
- **THEN** the status label shows a success message and the button reverts
  to "开始转码"; a `false` report shows a failure message instead

### Requirement: Transcoder callbacks are marshaled to the GUI thread
`Transcoder::SetProgressCallback`/`SetCompletionCallback` fire on the
graph's internal worker threads, not the Qt GUI thread. `TranscoderPage`
SHALL NOT touch any `QWidget` directly inside those callback lambdas;
each callback SHALL immediately hop to the GUI thread via
`QMetaObject::invokeMethod(this, [...]{ ... }, Qt::QueuedConnection)`
before updating any widget.

#### Scenario: Callback invoked off the GUI thread still updates safely
- **WHEN** the `MuxNode` fan-in thread invokes the progress hook while a
  transcode is running
- **THEN** the resulting widget update executes on the GUI thread via the
  queued invocation, with no direct cross-thread widget access
