## Why

The media graph currently only has an ingest path (`DemuxNode` → `DecoderNode`)
and playback sinks (`VideoSinkNode`/`AudioSinkNode`). There is no way to encode
processed frames back into a file, which blocks three product goals: recording,
transcoding, and push streaming. `EncoderNode` (in `graph-transform-nodes`) and
`MuxNode`/`Transcoder` (in `graph-transcode`) are already specced but have zero
implementation. Implementing them via the `Transcoder` facade first is the
simplest validation path — it needs no `Clock`/AV-sync (runs full speed),
so it isolates encode/mux correctness from playback timing concerns.

## What Changes

- Implement `EncoderNode` (Transform node, mirrors `DecoderNode`): wraps
  `avcodec_send_frame()`/`avcodec_receive_packet()`, software encode only
  (`libx264` for video, `aac` for audio), configurable rate-control mode
  (CRF or target bitrate), GOP size, max B-frames, encoder preset.
- Implement `MuxNode` (Sink node, mirrors `DemuxNode`): wraps
  `avformat_write_header()`/`av_interleaved_write_frame()`/`av_write_trailer()`,
  container inferred from the output file extension (no hardcoded format).
- Implement `Transcoder` facade (`include/mvp/transcoder.h`, new public API,
  Pimpl, independent of `MediaPlayer` — no shared `Clock`/`EffectManager`/graph
  instance): builds `FileSource → Demux → Decoder(V/A) → Encoder(V/A) → Mux →
  FileSink`, exposes `SetInput`/`SetOutput(path, TranscodeOptions)`/`Start`/
  `Cancel`/`SetProgressCallback`.
- Pin down the `TranscodeOptions` parameter shape (video: codec name, rate
  control mode, CRF/bitrate, GOP size, max B-frames, preset; audio: codec
  name, bitrate, sample rate, channels) — previously left unspecified in
  `graph-transcode`'s `SetOutput(...)`.
- Validation via unit tests / a small CLI smoke test only — **no Qt GUI
  integration in this change** (deferred to a follow-up change once the
  backend is verified stable).
- **v1 scope exclusions** (deferred to follow-up changes): no `AVFilterNode`
  wiring (no resolution/fps override, no effect chain during transcode), no
  trim (start/end time), no stream passthrough/copy, no two-pass encoding, no
  hardware encode (NVENC/QSV) — software encode (`libx264`/`aac`) only.

## Capabilities

### New Capabilities
(none — reuses the existing `EncoderNode` requirement in `graph-transform-nodes`
and `MuxNode`/`Transcoder` requirements in `graph-transcode`)

### Modified Capabilities
- `graph-transcode`: pins down `Transcoder::SetOutput`'s parameter shape via a
  concrete `TranscodeOptions` struct, adds `SetCompletionCallback`, and
  explicitly scopes v1 behavior (single video + single audio track, no
  filter/trim/passthrough/two-pass, software encode only). Also pins down
  `MuxNode`'s threading model (Active fan-in thread, single-threaded muxer
  access) and adds an internal progress hook.
- `graph-transform-nodes`: pins down `EncoderNode`'s `EncodeParams`
  configuration shape (codec name, CRF/bitrate rate control, GOP size, max
  B-frames, preset) and EOS-drain behavior.

## Impact

- New files: `src/media/nodes/encoder_node.{h,cc}`, `src/media/nodes/mux_node.{h,cc}`,
  `src/media/transcoder.cc`, `include/mvp/transcoder.h`.
- No changes to `mvp_app` (Qt GUI) in this change.
- No changes needed to `CMakeLists.txt` (glob-based source collection already
  picks up new files under `src/media/`).
- Depends only on existing FFmpeg encode/mux APIs already available via the
  `FFmpeg::FFmpeg` link (no new third-party dependency).
