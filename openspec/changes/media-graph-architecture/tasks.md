## 1. Phase 1: Core Graph Framework (media-graph-core + graph-node-lifecycle)

### 1.1 Data Carrier & Format

- [x] 1.1.1 Create `src/core/src/graph/` directory structure (moved from include/mvp/graph/ to internal — graph framework depends on FFmpeg types)
- [x] 1.1.2 Define `MediaType` enum in `media_buffer.h` (reuses existing from `media_frame.h`)
- [x] 1.1.3 Define `BufferFlags` enum (kEos, kDiscontinuity, kKeyFrame, kCorrupt)
- [x] 1.1.4 Define `Timestamp` struct (pts, dts, duration, time_base) + `Rational` struct
- [x] 1.1.5 Implement `MediaBuffer` class with `std::variant<std::monostate, AVPacketPtr, MediaFrame>` payload + metadata + move-only semantics
- [x] 1.1.6 Implement `MediaFormat` class with video/audio/packet sub-types + project-local enums
- [x] 1.1.7 Implement `FormatCaps` struct for capability ranges (pixel_formats, resolutions, sample_rates)

### 1.2 Link (Unified Queue)

- [x] 1.2.1 Implement `ByteCapacity` struct (max_bytes, static Size())
- [x] 1.2.2 Implement `CountCapacity` struct (max_count, static Size())
- [x] 1.2.3 Implement `Link<CapacityPolicy>` template (mutex + cond_push/cond_pop + atomic serial)
- [x] 1.2.4 Implement Link::Push() — blocking with capacity check, serial assignment
- [x] 1.2.5 Implement Link::Pop() — blocking with abort detection, returns `std::optional<MediaBuffer>`
- [x] 1.2.6 Implement Link::Flush() — clear queue + atomic serial increment
- [x] 1.2.7 Implement Link::Abort() / Reset()

### 1.3 Port

- [x] 1.3.1 Define `InputPort` class — caps, peer, link, Pull()
- [x] 1.3.2 Define `OutputPort` class — caps, Connect(), Push() with Passive routing
- [x] 1.3.3 Implement Connect() — caps intersection check + Link creation

### 1.4 INode Interface

- [x] 1.4.1 Define `NodeType` enum (kSource, kTransform, kSink)
- [x] 1.4.2 Define `ThreadingMode` enum (kPassive, kActive)
- [x] 1.4.3 Define `NodeState` enum (kIdle, kConfigured, kPrepared, kRunning, kPaused, kError)
- [x] 1.4.4 Define `INode` abstract interface — Configure/Negotiate/Prepare/Start/Stop/Flush + ports + attributes
- [x] 1.4.5 Define `NodeConfig` struct with typed fields for common node parameters

### 1.5 MediaGraph

- [x] 1.5.1 Implement `GraphEvent` enum (kEos, kError, kStateChanged, kFormatChanged)
- [x] 1.5.2 Implement `GraphState` enum (kIdle, kReady, kPlaying, kPaused, kFinished, kError)
- [x] 1.5.3 Implement `MediaGraph` class — AddNode/Connect
- [x] 1.5.4 Implement topological sort with cycle detection (Kahn's algorithm)
- [x] 1.5.5 Implement Negotiate() — topo-sort → Negotiate() per node
- [x] 1.5.6 Implement Prepare() / Start() / Stop() — cascade calls
- [x] 1.5.7 Implement Flush() — broadcast to all nodes and links
- [x] 1.5.8 Implement SetClock() / Clock() — global clock injection
- [x] 1.5.9 Implement EventCallback dispatch (ReportEvent)

### 1.6 Phase 1 Validation

- ~~1.6.1 Write unit tests: Link~~ (skipped)
- ~~1.6.2 Write unit tests: Port~~ (skipped)
- ~~1.6.3 Write unit tests: MediaGraph topo sort~~ (skipped)
- ~~1.6.4 Write unit tests: MediaGraph lifecycle~~ (skipped)
- [x] 1.6.5 Update `CMakeLists.txt` to include new graph source files (auto-glob handles it)
- [x] 1.6.6 Build and ensure no compilation errors

---

## 2. Phase 2: Basic Nodes & Playback (graph-source-nodes + graph-sink-nodes + graph-playback)

### 2.1 Source Nodes

- [x] 2.1.1 Create `src/core/src/nodes/` directory structure
- [x] 2.1.2 Implement `DemuxNode` (INode, kSource, kActive) — avformat_open_input + avformat_find_stream_info in Prepare(), av_read_frame loop in worker thread, dynamic output ports per stream
- [x] 2.1.3 Implement DemuxNode::Flush() — mark seek pending, worker thread calls avformat_seek_file on next iteration

### 2.2 Decoder Node

- [x] 2.2.1 Implement `DecoderNode` (INode, kTransform, kActive) — avcodec_send/receive loop
- [x] 2.2.2 Implement DecoderNode hardware acceleration (D3D11VA via HWAccelContext)
- [x] 2.2.3 Implement DecoderNode::SetDropUntilPts() for seek optimization
- [x] 2.2.4 Implement DecoderNode EOF handling — flush codec buffers, emit kEos

### 2.3 Sink Nodes

- [x] 2.3.1 Implement `VideoSinkNode` (INode, kSink, kActive) — full frame_timer AV sync logic from PlayerImpl
- [x] 2.3.2 Implement VideoSinkNode D3D11/NV12/YUV420P/swscale render paths (delegates to VideoRenderer)
- [x] 2.3.3 Implement `AudioSinkNode` (INode, kSink, kActive) — SDL audio with SwrContext resampling
- [x] 2.3.4 Implement AudioSinkNode audio_clock update for MasterClock

### 2.4 MediaPlayer API

- [x] 2.4.1 Create `src/core/include/mvp/media_player.h` — public header
- [x] 2.4.2 Implement `MediaPlayer` class — Open builds playback graph
- [x] 2.4.3 Implement Play/Pause/Stop — delegate to MediaGraph
- [x] 2.4.4 Implement Seek() — Flush + DemuxNode seek + DecoderNode SetDropUntilPts
- [x] 2.4.5 Implement CurrentPosition() / Duration() — delegate to Clock
- [x] 2.4.6 Implement SetVideoCallback / SetAudioCallback

### 2.5 Phase 2 Cleanup

- [x] 2.5.1 Update `app/` code to use MediaPlayer instead of Player
- [x] 2.5.2 Remove old files: `player.h`, `player.cc`, `i_decoder.h`, `decoder.h`, `decoder.cc`, `demuxer.h`, `demuxer.cc`, `stream_context.h`, `stream_context.cc`, `packet_queue.h`, `packet_queue.cc`, `frame_queue.h`, `frame_queue.cc`, `audio_renderer.h`, `audio_renderer.cc`, `player_state.h`
- [x] 2.5.3 Update `CMakeLists.txt` — glob auto-picks up new files, old files removed
- [x] 2.5.4 Build and verify compilation (0 errors, 0 warnings)

### 2.6 Phase 2 Validation

- [ ] 2.6.1 Integration test: Play MP4 file (H264+AAC) with audio/video sync
- [ ] 2.6.2 Integration test: Seek to multiple positions, verify correct frame display
- [ ] 2.6.3 Integration test: Play pure video file (no audio stream, VideoMaster mode)
- [ ] 2.6.4 Integration test: EOF detection and Finished state transition
- [ ] 2.6.5 Integration test: Pause/Resume without glitches

---

## 3. Phase 3: Filter Chain (graph-transform-nodes)

### 3.1 AVFilterNode

- [ ] 3.1.1 Implement `AVFilterNode` (INode, kTransform, kPassive by default)
- [ ] 3.1.2 Implement filter description parser — `"scale=1280:720,eq=brightness=0.1"` → AVFilterGraph
- [ ] 3.1.3 Implement Negotiate() — configure buffersrc from input format, read buffersink output format
- [ ] 3.1.4 Implement Process(MediaBuffer) — av_buffersrc_add_frame → av_buffersink_get_frame
- [ ] 3.1.5 Implement runtime filter parameter update (Reconfigure)
- [ ] 3.1.6 Implement `FilterChainBuilder` helper class

### 3.2 MediaPlayer Filter Integration

- [ ] 3.2.1 Implement MediaPlayer::SetFilter() — Stop current graph → rebuild graph with AVFilterNode between Decoder and Sink → Seek to saved position → Start
- [ ] 3.2.2 Handle filter removal (SetFilter("") rebuilds graph without filter node)

### 3.3 Phase 3 Validation

- [ ] 3.3.1 Test: Apply scale filter, verify output resolution change
- [ ] 3.3.2 Test: Apply eq filter, verify brightness/contrast change
- [ ] 3.3.3 Test: Chained filters (scale + eq) in single description string
- [ ] 3.3.4 Test: Runtime filter parameter update without restart
- [ ] 3.3.5 Test: Passive mode — verify 0 additional threads created (compare thread count before/after filter insertion)

---

## 4. Phase 4: Transcoding (graph-transcode)

### 4.1 Encoder & Mux Nodes

- [ ] 4.1.1 Implement `EncoderNode` (INode, kTransform, kActive) — avcodec_send_frame/receive_packet for encoding
- [ ] 4.1.2 Implement `MuxNode` (INode, kTransform, kActive) — avformat_write_header/av_interleaved_write_frame/av_write_trailer
- [ ] 4.1.3 Implement `FileSinkNode` (INode, kSink, kActive) — file I/O for output
- [ ] 4.1.4 Implement EncoderNode EOF flush — send nullptr frame to flush encoder delay frames

### 4.2 Transcoder API

- [ ] 4.2.1 Create `src/core/include/mvp/transcoder.h` — public header
- [ ] 4.2.2 Implement `Transcoder` class — build transcode graph (no Clock injection, full-speed mode)
- [ ] 4.2.3 Implement progress callback — track input duration vs processed position
- [ ] 4.2.4 Implement Cancel() — MediaGraph::Stop()

### 4.3 Phase 4 Validation

- [ ] 4.3.1 Test: MP4(H264+AAC) → MP4(H265+AAC) transcode, verify output plays correctly
- [ ] 4.3.2 Test: Transcode with scale filter (e.g., 1920x1080 → 1280x720)
- [ ] 4.3.3 Test: Progress callback reaches 100%
- [ ] 4.3.4 Test: Cancel mid-transcode and verify cleanup

---

## 5. Documentation & Polish

- [ ] 5.1 Update `README.md` — describe new graph architecture, usage examples for MediaPlayer/Transcoder
- [ ] 5.2 Record architectural decisions to `docs/` (if not already in design.md)
- [ ] 5.3 Verify all `docs/improvements/` entries related to pipeline/stream-context are marked resolved
