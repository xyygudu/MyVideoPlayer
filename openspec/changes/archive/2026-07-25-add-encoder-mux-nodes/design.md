## Context

The graph engine (`MediaGraph`/`INode`/`Port`/`Link`/`MediaBuffer`, see
`media-graph-core`) currently only has an ingest path: `DemuxNode` (Source,
Active, fans packets out to per-stream output ports) → `DecoderNode`
(Transform, Active, decodes packets to frames) → playback sinks
(`VideoSinkNode`/`AudioSinkNode`). `EncoderNode` and `MuxNode` are specced
(`graph-transform-nodes`, `graph-transcode`) but unimplemented. This change
builds them and a `Transcoder` facade that assembles:

```
FileSource → Demux ─┬─► Decoder(V) → Encoder(V) ─┐
                    └─► Decoder(A) → Encoder(A) ──┤
                                                    └─► Mux → FileSink
```

`Transcoder` is a standalone facade (own `MediaGraph` instance, no shared
`Clock`/`EffectManager` with `MediaPlayer`), matching the toolbox direction
already agreed for this project: shared engine/node code, independent
facade-layer instances.

## Goals / Non-Goals

**Goals:**
- Implement `EncoderNode` (software encode: `libx264` video, `aac` audio)
  with configurable rate control (CRF or target bitrate), GOP size, max
  B-frames, encoder preset.
- Implement `MuxNode` writing an interleaved output file, container inferred
  from the output path extension.
- Implement `Transcoder` public API (`SetInput`, `SetOutput(path,
  TranscodeOptions)`, `Start`, `Cancel`, `SetProgressCallback`,
  `SetCompletionCallback`) that builds and runs the graph above end-to-end.
- Validate correctness via unit tests / a CLI smoke test transcoding a real
  sample file.

**Non-Goals (deferred to follow-up changes):**
- Qt GUI integration (parameter panel, progress UI).
- `AVFilterNode` wiring — no resolution/fps override, no effect chain during
  transcode.
- Trim (start/end time), stream passthrough/copy, two-pass encoding.
- Hardware encode (NVENC/QSV) — software only for now.
- Multiple video/audio tracks — v1 handles exactly one video + one audio
  stream (or either alone).

## Decisions

### 1. `MuxNode` is `ThreadingMode::kActive` with its own fan-in thread

**Decision:** `MuxNode` owns a worker thread that pulls from N input Links
(one per stream) and picks the next packet to write by comparing PTS across
inputs, mirroring `DemuxNode`'s fan-out thread but in reverse (fan-in).

**Alternative considered:** `kPassive`, invoked synchronously by whichever
upstream `EncoderNode`'s Active thread has data ready. Rejected because
`av_interleaved_write_frame`/`avformat_write_header` are not safe to call
concurrently from two different encoder threads (video encoder thread and
audio encoder thread would race on the same `AVFormatContext`). Giving
`MuxNode` its own thread makes all muxer calls single-threaded by
construction — no mutex needed — and keeps the source/sink sides of the
graph symmetric (fan-out thread ↔ fan-in thread).

### 2. `EncoderNode` mirrors `DecoderNode` structure exactly

Same shape as `DecoderNode`: single input/output port, `ThreadingMode::kActive`,
`AVCodecContext` allocated in `Prepare()` and freed in `Stop()`, `Flush()`
calls `avcodec_flush_buffers`, EOF handled by sending a null frame to drain
the encoder then pushing an EOS `MediaBuffer`. Configuration is constructor/
setter-based (per `INode`'s existing convention that config is node-specific,
not part of the polymorphic interface), taking an `EncodeParams` struct:

```cpp
struct EncodeParams {
    std::string codec_name;                 // "libx264" / "aac"
    RateControlMode rate_control{RateControlMode::kCrf};
    int crf{23};
    int64_t bitrate_bps{0};                 // used when rate_control == kBitrate
    int gop_size{250};
    int max_b_frames{2};
    std::string preset{"medium"};           // libx264 preset; ignored by aac
};
```

CRF/preset are libx264-specific private options, set via `AVDictionary`
passed to `avcodec_open2`; `gop_size`/`max_b_frames`/`bit_rate` are generic
`AVCodecContext` fields.

### 3. `TranscodeOptions` pins down `Transcoder::SetOutput`'s parameter shape

```cpp
struct TranscodeOptions {
    EncodeParams video;   // codec_name empty => no video output stream
    EncodeParams audio;   // codec_name empty => no audio output stream
};
```

This is the one requirement-level change to the existing `graph-transcode`
spec — `SetOutput(...)` was previously left with an unspecified parameter
list. No resolution/frame-rate/trim fields are included in v1 (see
Non-Goals); they will be added alongside `AVFilterNode`/trim support in a
follow-up change rather than added now as unused fields.

### 4. Progress and completion reporting

`MuxNode` exposes an internal (non-public-API) hook,
`SetProgressHook(std::function<void(double pts_seconds)>)`, invoked after
each successful interleaved write of the longer/primary stream. `Transcoder`
wires this hook, divides by the source duration (from `DemuxNode::Duration()`)
to compute a percentage, and forwards it to the user's `ProgressCallback`.
Overall success/failure is reported via `SetCompletionCallback(std::function
<void(bool ok)>)`, driven by the graph's existing `GraphEvent::kFinished` /
`GraphEvent::kError` events — this mirrors `MediaPlayer::SetPlaybackFinishedCallback`'s
existing naming/shape for consistency across facades.

### 5. Error handling

Per project convention, encode/mux failures (e.g. `avcodec_send_frame`
returning an error, `avformat_write_header` failing to open the output) are
logged via `SPDLOG_ERROR`, transition the node to `NodeState::kError`, and
propagate as `GraphEvent::kError` → `Transcoder`'s `SetCompletionCallback(false)`.
No silent failures; no automatic retry (retrying mid-transcode is out of
scope — user re-runs `Transcoder::Start()`).

## Risks / Trade-offs

- **[Risk]** Single fan-in mux thread could become a throughput bottleneck if
  encoding is very fast (e.g. tiny files) → **Mitigation**: acceptable for v1
  given software encode is already the bottleneck; revisit only if hardware
  encode (future change) makes mux thread the limiter.
- **[Risk]** `EncodeParams` growing many fields for future formats (HDR,
  10-bit, hardware encode) → **Mitigation**: struct is additive-only
  (new optional fields with defaults), no breaking change expected.
- **[Trade-off]** No trim/passthrough in v1 means the CLI smoke test can only
  validate whole-file re-encode, not partial scenarios — acceptable since
  those are explicitly deferred, and whole-file re-encode is the strictly
  larger, harder-to-get-right code path (partial/passthrough add restrictions
  on top of a working full path, not new fundamental mechanisms).

## Migration Plan

Purely additive — no existing code paths are modified. `mvp_core`'s CMake
glob picks up the new files automatically. No rollback concerns beyond
reverting the new files.

## Open Questions

None outstanding — scope was confirmed with the project owner (software
encode only, no GUI this change, no trim/passthrough/two-pass, single
video+audio track).
