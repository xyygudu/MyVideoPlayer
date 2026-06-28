## Purpose

Defines the source nodes (DemuxNode, DecoderNode) that ingest media data into
the graph pipeline — opening media files, demuxing streams, and decoding
compressed packets into raw frames.

## Requirements

### Requirement: DemuxNode opens file and routes packets by stream
系统 SHALL 定义 `DemuxNode`（**Source 类型**），负责打开媒体文件并按流分发压缩包。

DemuxNode SHALL 提供：
- 无输入端口（Source 节点）
- 动态输出端口：Prepare() 阶段按文件内流数量创建
- Prepare()：调用 `avformat_open_input()` + `avformat_find_stream_info()`
- Start()：启动内部线程循环调用 `av_read_frame()`
- ThreadingMode：Active

DemuxNode SHALL 实现 seek：Flush() 时丢弃内部缓冲，工作线程在下次循环时执行 `avformat_seek_file()`。

#### Scenario: Demux routes video/audio packets to separate ports
- **WHEN** 输入文件含 1 个视频流和 1 个音频流
- **THEN** av_read_frame 后按 stream_index 路由

#### Scenario: Demux emits EOS buffer on EOF
- **WHEN** av_read_frame() 返回 AVERROR_EOF
- **THEN** 每个输出端口 Push 一个 flags=kEos 的 MediaBuffer

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
