## Purpose

Defines the Transcoder class and MuxNode for transcode scenarios —
building transcode graphs that process media from source to encoded output.

## Requirements

### Requirement: Transcoder builds transcode graph
系统 SHALL 定义 `Transcoder` 类，作为转码场景的预设图构建器。

Transcoder SHALL 提供：
- `SetInput(const std::string& url)` / `SetOutput(...)` / `SetFilter(...)`
- `Start()`：构建图并启动（无 Clock，全速处理）
- `Cancel()` / `SetProgressCallback(ProgressCallback)`

Transcoder 内部图构建：
```
FileSource → Demux ─┬─► Decoder(V) → [optional AVFilter] → Encoder(V) ─┐
                    └─► Decoder(A) → Encoder(A) ────────────────────────┤
                                                                         └─► MuxNode → FileSink
```

#### Scenario: Progress callback reports percentage
- **WHEN** 输入 60 秒文件，全速转码
- **THEN** ProgressCallback 被多次调用，百分比从 0 递增到 100

### Requirement: MuxNode multiplexes streams
系统 SHALL 定义 `MuxNode`（Transform/Sink 类型），使用 FFmpeg `avformat_write_header()` / `av_interleaved_write_frame()` / `av_write_trailer()`。

#### Scenario: Mux interleaves video and audio
- **WHEN** 视频和音频编码帧按 PTS 到达
- **THEN** av_interleaved_write_frame 按 PTS 排序写入

#### Scenario: Write trailer on stop
- **WHEN** 所有输入端口均收到 kEos
- **THEN** av_write_trailer() 被调用
