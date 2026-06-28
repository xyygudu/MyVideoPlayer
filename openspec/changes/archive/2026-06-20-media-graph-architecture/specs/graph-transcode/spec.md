## ADDED Requirements

### Requirement: Transcoder builds transcode graph
系统 SHALL 定义 `Transcoder` 类，作为转码场景的预设图构建器。

Transcoder SHALL 提供：
- `SetInput(const std::string& url)`：输入文件路径
- `SetOutput(const std::string& url, TranscodeConfig config)`：输出文件路径 + 编码配置（视频编码器、音频编码器、比特率等）
- `SetFilter(const std::string& desc)`：可选滤镜链
- `Start()`：构建图并启动（无 Clock，全速处理）
- `Cancel()`：停止转码
- `SetProgressCallback(ProgressCallback)`：进度报告（0.0 ~ 1.0）

Transcoder 内部图构建：
```
FileSource → Demux ─┬─► Decoder(V) → [optional AVFilter] → Encoder(V) ─┐
                    └─► Decoder(A) → Encoder(A) ────────────────────────┤
                                                                         └─► MuxNode → FileSink
```

#### Scenario: Transcode MP4 H264 to H265
- **WHEN** 输入 H264+AAC MP4，配置输出编码器为 hevc_nvenc
- **THEN** 输出 H265+AAC MP4 文件，可正常播放

#### Scenario: Progress callback reports percentage
- **WHEN** 输入 60 秒文件，全速转码
- **THEN** ProgressCallback 被多次调用，百分比从 0 递增到 100

#### Scenario: Cancel stops immediately
- **WHEN** 转码进行到 50%，调用 Cancel()
- **THEN** MediaGraph::Stop() 被调用，输出文件关闭（可能不完整）

#### Scenario: Transcode with filter
- **WHEN** 设置 SetFilter("scale=640:480"), 输入 1920x1080
- **THEN** 输出视频分辨率为 640x480

### Requirement: MuxNode multiplexes streams
系统 SHALL 定义 `MuxNode`（Transform/Sink 类型），使用 FFmpeg `avformat_write_header()` / `av_interleaved_write_frame()` / `av_write_trailer()` 将多路压缩流交织写入容器。

MuxNode SHALL 提供：
- 多个输入端口（按流类型，通常 1 视频 + 1 音频）
- Configure 参数：输出文件路径、容器格式（MP4/MKV 等）
- Prepare()：创建输出 AVFormatContext，avformat_write_header()
- ThreadingMode：Active

#### Scenario: Mux interleaves video and audio
- **WHEN** 视频和音频编码帧按 PTS 到达
- **THEN** av_interleaved_write_frame 按 PTS 排序写入

#### Scenario: Write trailer on stop
- **WHEN** 两个输入端口均收到 kEos
- **THEN** av_write_trailer() 被调用，文件头信息正确写入
