## Purpose

Defines the source nodes (DemuxNode, DecoderNode) that ingest media data into
the graph pipeline — opening media files, demuxing streams, and decoding
compressed packets into raw frames.
## Requirements
### Requirement: DemuxNode opens file and routes packets by stream
系统 SHALL 定义 `DemuxNode`（**Source 类型**），负责打开媒体文件并按流分发压缩包。

DemuxNode SHALL 提供：
- 无输入端口（Source 节点）
- 输出端口：构造时按选定的流索引创建（视频端口在前，音频端口在后）
- `Open()`：调用 `avformat_open_input()` + `avformat_find_stream_info()`，保留幂等守卫
- `Negotiate()`：纯格式推理——读取已打开上下文的 `codecpar` 发布各输出端口格式，SHALL NOT 打开文件或分配资源；访问 `format_ctx_->streams[index]` 前 SHALL 校验 index 小于 `nb_streams`，越界返回 false
- `Start()`：启动内部线程循环调用 `av_read_frame()`
- ThreadingMode：Active

DemuxNode SHALL 实现 seek：Flush() 时丢弃内部缓冲，工作线程在下次循环时执行 `avformat_seek_file()`。

#### Scenario: Demux routes video/audio packets to separate ports
- **WHEN** 输入文件含 1 个视频流和 1 个音频流
- **THEN** av_read_frame 后按 stream_index 路由

#### Scenario: Demux emits EOS buffer on EOF
- **WHEN** av_read_frame() 返回 AVERROR_EOF
- **THEN** 每个输出端口 Push 一个 flags=kEos 的 MediaBuffer

#### Scenario: File opened in Open phase not negotiation
- **WHEN** MediaGraph::Open() 调用 DemuxNode::Open()
- **THEN** 文件被打开；随后的 Negotiate() 不再调用 avformat_open_input

#### Scenario: Stream index out of range fails negotiation
- **WHEN** Negotiate 中 video_stream_index >= nb_streams
- **THEN** 记录 ERROR 日志并返回 false

### Requirement: DecoderNode decodes compressed packets to frames
系统 SHALL 定义 `DecoderNode`（Transform 类型），使用 FFmpeg `avcodec_send_packet()` / `avcodec_receive_frame()` 解码。

DecoderNode SHALL 提供：
- 单个输入端口 + 单个输出端口
- Prepare()：创建 AVCodecContext，打开解码器
- 保留 seek 优化：`SetDropUntilPts(double)`
- ThreadingMode：Active

#### Scenario: Decode video packet to frame
- **WHEN** 输入一个视频 AVPacket，解码器产生一个 AVFrame
- **THEN** 输出端口 Push 一个 payload 为 MediaFrame 的 MediaBuffer

#### Scenario: EOS propagates through decoder
- **WHEN** 输入端口收到 flags=kEos 的 MediaBuffer
- **THEN** 调用 avcodec_send_packet(nullptr) 冲刷解码器缓冲，最后输出 kEos

### Requirement: DemuxNode 时间轴从 0 开始
DemuxNode SHALL 将每个输出包的 pts/dts 减去容器 `start_time`，使时间轴从 0 开始（MPEG-TS 常从 1.4 秒或任意偏移开始）；seek 目标 SHALL 按同一偏移换算回容器时间。

#### Scenario: 起点为 10 秒的 TS
- **WHEN** 播放时间戳从 10 秒开始的 TS 文件
- **THEN** 起播时 `CurrentPosition()` 与屏幕帧 pts 都小于 2 秒，`Duration()` 约为文件实际时长

### Requirement: DemuxNode 可选折叠时间戳跳变
DemuxNode SHALL 接受 `DemuxOptions{correct_ts_discontinuities}`（默认 false）。开启且容器带 `AVFMT_TS_DISCONT` 标志时，DemuxNode SHALL 按流跟踪期望的下一个 DTS，当实际 DTS 偏离超过阈值或明显后退时，把偏差累加进一个跨流共享的偏移量并平移此后所有包（ffmpeg 命令行 `ts_discontinuity_detect` 语义），记录告警；seek 时 SHALL 清空跟踪状态与偏移。播放 SHALL 不开启（ffplay 语义）。

#### Scenario: 折叠后跳
- **WHEN** 开启折叠，某流 DTS 从 4.4 秒跳回 1.4 秒
- **THEN** 后续包整体平移 3 秒，输出时间轴连续，日志记录一次跳变

