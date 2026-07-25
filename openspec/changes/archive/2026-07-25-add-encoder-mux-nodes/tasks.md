## 1. Shared config types

- [x] 1.1 Add `RateControlMode` enum (`kCrf`, `kBitrate`) and `EncodeParams`
      struct (`codec_name`, `rate_control`, `crf`, `bitrate_bps`, `gop_size`,
      `max_b_frames`, `preset`) — place alongside existing graph config types
      (e.g. `src/media/graph/media_format.h` or a new small header consumed by
      both `EncoderNode` and `Transcoder`).
- [x] 1.2 Add `TranscodeOptions` struct (`EncodeParams video`, `EncodeParams audio`)
      to the public API header `include/mvp/transcoder.h`.

## 2. EncoderNode

- [x] 2.1 Create `src/media/nodes/encoder_node.h`/`.cc` mirroring
      `DecoderNode`'s shape (single input/output port, `ThreadingMode::kActive`,
      own worker thread, `NodeState` transitions).
- [x] 2.2 Implement `Prepare()`: `avcodec_find_encoder_by_name(codec_name)`,
      allocate `AVCodecContext`, apply `EncodeParams` (CRF/preset via
      `AVDictionary` for libx264, `bit_rate`/`gop_size`/`max_b_frames` via
      direct fields), `avcodec_open2`.
- [x] 2.3 Implement encode loop: `avcodec_send_frame`/`avcodec_receive_packet`,
      wrap resulting `AVPacket` in `MediaBuffer`, push to output port.
- [x] 2.4 Implement EOS handling: on input EOS, `avcodec_send_frame(nullptr)`,
      drain remaining packets, then push an EOS `MediaBuffer`.
- [x] 2.5 Implement `Flush()` (`avcodec_flush_buffers`) and `Stop()`
      (free `AVCodecContext`, join worker thread).
- [x] 2.6 On any FFmpeg error, `SPDLOG_ERROR` with context and transition to
      `NodeState::kError` (no silent failure).
- [x] 2.7 Video: if an incoming frame's pixel format isn't the encoder's
      expected input (`AV_PIX_FMT_YUV420P` for `libx264`), lazily create a
      `SwsContext` and convert via `mvp::MediaFramePool` (reused mechanism,
      matches its documented single-thread-owner use case) before encoding.
- [x] 2.8 Audio: if an incoming frame's sample format/rate/layout doesn't
      match the encoder's expected input, lazily create a `SwrContext` and
      resample into a freshly allocated `AVFrame` before encoding (mirrors
      `AudioSinkNode::ConvertAndFeed`'s resample pattern).

## 3. MuxNode

- [x] 3.1 Create `src/media/nodes/mux_node.h`/`.cc`: N input ports (created
      per stream added), no output ports, `NodeType::kSink`,
      `ThreadingMode::kActive` with its own fan-in worker thread.
- [x] 3.2 Implement `Prepare()`: `avformat_alloc_output_context2` (format
      inferred from output path extension), create one `AVStream` per input
      port from negotiated `MediaFormat`, `avformat_write_header`.
- [x] 3.3 Implement fan-in loop: pull from all input Links, pick the buffer
      with the lowest PTS among those currently available, call
      `av_interleaved_write_frame` — single thread, no mutex needed around
      the muxer calls.
- [x] 3.4 Implement `SetProgressHook(std::function<void(double)>)`, invoked
      after each successful write of the primary stream (video if present,
      else audio).
- [x] 3.5 Implement trailer-on-EOS: once every input port has signaled EOS,
      call `av_write_trailer`, close the output file, transition to
      `NodeState`/report finished.
- [x] 3.6 On any FFmpeg error, `SPDLOG_ERROR` and transition to
      `NodeState::kError`.

## 4. Transcoder facade

- [x] 4.1 Create `include/mvp/transcoder.h` (Pimpl, `MVP_CORE_EXPORT`,
      independent of `MediaPlayer` — no shared `Clock`/`EffectManager`/
      `MediaGraph` instance): `SetInput`, `SetOutput`, `Start`, `Cancel`,
      `SetProgressCallback`, `SetCompletionCallback`.
- [x] 4.2 Create `src/media/transcoder.cc` implementing `Impl::BuildGraph`:
      probe input via existing `SourceProbe`, create `DemuxNode` →
      `DecoderNode`(s) → `EncoderNode`(s) → `MuxNode`, skipping the audio (or
      video) branch entirely when the corresponding `EncodeParams::codec_name`
      is empty.
- [x] 4.3 Wire `MuxNode::SetProgressHook` to compute `pts / duration * 100`
      and invoke the user's `ProgressCallback`.
- [x] 4.4 Wire the graph's `EventCallback` (`kFinished`/`kError`) to invoke
      `SetCompletionCallback` exactly once.
- [x] 4.5 Implement `Cancel()`: `MediaGraph::Stop()` and report completion
      with `ok=false`.

## 5. Validation

- [x] 5.1 Add a small standalone CLI smoke-test executable (new CMake target,
      e.g. `mvp_transcode_cli`, linking only `mvp_core` — no Qt) that takes
      input/output paths and a hardcoded `TranscodeOptions`, runs
      `Transcoder`, and prints progress/completion to stdout. (No gtest/Catch2
      introduced — matches existing project convention of manual/CLI
      validation over adding a new test framework.)
- [x] 5.2 Manually run the CLI against a real sample file (reuse an existing
      test asset if present) and confirm the output file plays back
      correctly in `mvp_app`.

      **Result**: Built `mvp_transcode_cli` and ran it against a local
      1920x1080 H.264/AAC `.mov` sample (`big_buck_bunny_1080p_h264.mov`).
      Transcode completed at 100% progress, exit code 0. Verified the output
      with `ffprobe` (valid h264 1920x1080 video stream + aac 48kHz/6ch audio
      stream, correct duration) and with `ffmpeg -f null -` (decodes the
      entire output with zero errors) as an automated proxy for playback
      correctness, since no GUI automation harness is available in this
      environment. Large output files were deleted after validation.

      **Known v1 limitation found during validation**: sources with an odd
      pixel width (e.g. the 853x480 `.mov` sample) fail because `libx264`
      requires even dimensions — this needs `AVFilterNode`-based
      cropping/scaling, which is explicitly out of scope for v1 (see design.md
      Non-Goals). Not a bug in this change; documented here for the
      follow-up change that adds `AVFilterNode` wiring.
- [x] 5.3 Verify every new function stays within the 50-line limit; split
      any that don't (per project function-length rule) in the same change.

      **Result**: Checked all new functions in `encoder_node.cc`,
      `mux_node.cc`, and `transcoder.cc` — longest is `BuildGraph` at ~42
      lines. No splits needed.

## 6. Docs

- [x] 6.1 Update `README.md`'s architecture diagram/feature list only if the
      CLI tool becomes a documented entry point (skip if it's purely a dev
      smoke test); otherwise no doc changes needed since GUI integration is
      deferred to a follow-up change.

      **Result**: Skipped — `mvp_transcode_cli` is a dev-only smoke-test tool,
      not a documented user-facing entry point. No README changes needed.
