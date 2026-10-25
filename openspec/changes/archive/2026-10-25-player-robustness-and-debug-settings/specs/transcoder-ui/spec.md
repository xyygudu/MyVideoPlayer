## MODIFIED Requirements

### Requirement: Start/cancel and progress reporting
`TranscoderPage` SHALL provide a "开始转码" button that constructs
`TranscodeOptions` from the current controls and calls
`Transcoder::SetInput`/`SetOutput`/`Start`, then a progress bar (0-100) and
status label. While running, the start button SHALL be replaced by a
"取消" button calling `Transcoder::Cancel()`.

#### Scenario: Progress bar updates during transcode
- **WHEN** `Transcoder`'s progress callback reports 42.0
- **THEN** the progress bar shows 42% (see cross-thread requirement below)

#### Scenario: Completion shows success, cancellation or failure status
- **WHEN** the completion callback reports `TranscodeResult::kSucceeded`
- **THEN** the status label shows a success message and the button reverts
  to "开始转码"; `kCancelled` shows a cancelled message and `kFailed` a
  failure message instead

### Requirement: Transcoder callbacks are marshaled to the GUI thread
`TranscoderPage` SHALL NOT touch any `QWidget` directly inside the
`Transcoder` callback lambdas: progress fires on the muxer thread, and
completion fires on the transcoder's event thread when a run ends by itself
(on the calling thread for `Cancel()` and failed `Start()`). Each callback
SHALL immediately hop to the GUI thread via
`QMetaObject::invokeMethod(this, [...]{ ... }, Qt::QueuedConnection)`
before updating any widget.

#### Scenario: Callback invoked off the GUI thread still updates safely
- **WHEN** the `MuxNode` fan-in thread invokes the progress hook while a
  transcode is running
- **THEN** the resulting widget update executes on the GUI thread via the
  queued invocation, with no direct cross-thread widget access
