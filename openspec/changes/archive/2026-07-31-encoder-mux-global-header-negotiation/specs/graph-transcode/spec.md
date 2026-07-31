## MODIFIED Requirements

### Requirement: MuxNode multiplexes streams
系统 SHALL 定义 `MuxNode`（Sink 类型），使用 FFmpeg `avformat_write_header()` / `av_interleaved_write_frame()` / `av_write_trailer()`。

MuxNode SHALL 提供：
- N 个输入端口（每路输出媒体流一个），无输出端口
- ThreadingMode：Active——自身持有一个 fan-in 工作线程，从所有输入 Link 按 PTS 选取下一个待写入的包，避免多个上游 Encoder 线程并发调用同一个 AVFormatContext
- 容器格式由输出文件扩展名推断，不写死具体格式
- 可选的进度钩子：`SetProgressHook(std::function<void(double pts_seconds)>)`，每次成功写入主流（视频优先，否则音频）的包后调用
- 协商期容器需求：`Negotiate()` SHALL 用 `av_guess_format` 解析输出容器，把 `AVFMT_GLOBALHEADER` 结论通过 `SetNeedsGlobalHeader` 写入各输入端口，供上游 Encoder 在 Prepare 期消费

#### Scenario: Mux interleaves video and audio
- **WHEN** 视频和音频编码帧按 PTS 到达
- **THEN** av_interleaved_write_frame 按 PTS 排序写入

#### Scenario: Write trailer on stop
- **WHEN** 所有输入端口均收到 kEos
- **THEN** av_write_trailer() 被调用

#### Scenario: Single-threaded muxer access
- **WHEN** 视频 Encoder 线程和音频 Encoder 线程同时有数据到达 MuxNode 的输入 Link
- **THEN** MuxNode 自身的 fan-in 线程串行地从两个 Link 取数据并调用 av_interleaved_write_frame，不存在两个线程同时调用 FFmpeg muxer API 的情况

#### Scenario: MuxNode publishes global-header requirement in negotiation
- **WHEN** `Negotiate()` 解析输出为 matroska（含 `AVFMT_GLOBALHEADER`）
- **THEN** 各输入端口被设置为 `needs_global_header=true`；解析为 mpegts 时则为 false

#### Scenario: Unknown container falls back to in-band headers
- **WHEN** `av_guess_format` 无法推断输出格式
- **THEN** 记录 WARN，各输入端口 `needs_global_header=false`，不阻塞转码
