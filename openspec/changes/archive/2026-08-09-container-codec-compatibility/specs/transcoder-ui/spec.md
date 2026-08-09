## MODIFIED Requirements

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
