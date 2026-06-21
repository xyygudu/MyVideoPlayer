## ADDED Requirements

### Requirement: DemuxNode opens file and routes packets by stream
系统 SHALL 定义 `DemuxNode`（**Source 类型**），负责打开媒体文件并按流分发压缩包。DemuxNode 合并了文件打开和解复用两个职责，因为 FFmpeg 的 `avformat_open_input()` 和 `av_read_frame()` 紧耦合于同一个 `AVFormatContext`，拆分为两个节点会导致 MediaBuffer variant 无法承载 `AVFormatContext*`。

DemuxNode SHALL 提供：
- 无输入端口（Source 节点）
- 动态输出端口：Prepare() 阶段按文件内流数量创建，每个输出端口对应一个媒体流（索引与 AVStream::index 一致）
- 每个输出端口携带流格式元数据（MediaFormat 含 codec_id, time_base, stream_index）
- Configure 参数：文件路径字符串
- Prepare()：调用 `avformat_open_input()` + `avformat_find_stream_info()`，创建输出端口
- Start()：启动内部线程循环调用 `av_read_frame()`
- ThreadingMode：Active

DemuxNode SHALL 实现 seek：Flush() 时丢弃内部缓冲 + 标记 seek 请求，工作线程在下次循环时执行 `avformat_seek_file()`。

#### Scenario: Open a valid media file
- **WHEN** 使用有效文件路径调用 Configure + Prepare
- **THEN** Prepare() 返回 true，State() 变为 Prepared，输出端口按流数量创建

#### Scenario: Open invalid file returns error
- **WHEN** 使用不存在的文件路径
- **THEN** Prepare() 返回 false，State() 变为 Error，spdlog 记录错误

#### Scenario: Demux routes video/audio packets to separate ports
- **WHEN** 输入文件含 1 个视频流（index=0）和 1 个音频流（index=1）
- **THEN** av_read_frame 后按 stream_index 路由视频包到端口 0、音频包到端口 1

#### Scenario: Demux emits EOS buffer on EOF
- **WHEN** av_read_frame() 返回 AVERROR_EOF
- **THEN** 每个输出端口 Push 一个 flags=kEos 的 MediaBuffer

#### Scenario: DemuxNode has no input ports
- **WHEN** 查询 DemuxNode 的 Inputs()
- **THEN** 返回空 span（Source 类型无输入）

### Requirement: DecoderNode decodes compressed packets to frames
系统 SHALL 定义 `DecoderNode`（Transform 类型），使用 FFmpeg `avcodec_send_packet()` / `avcodec_receive_frame()` 接口解码，输出 MediaFrame 包裹的 AVFrame。

DecoderNode SHALL 提供：
- 单个输入端口：接收 AVPacket 的 MediaBuffer
- 单个输出端口：输出 MediaFrame 的 MediaBuffer
- Configure 参数：可选的 HWAccelContext（硬件加速）
- Prepare()：创建 AVCodecContext，打开解码器
- 保留 seek 优化：`SetDropUntilPts(double)` 用于快速跳过非参考帧
- ThreadingMode：Active

DecoderNode SHALL 在输出端口声明其解码后的 MediaFormat（分辨率、像素格式、采样率等）。

#### Scenario: Decode video packet to frame
- **WHEN** 输入一个视频 AVPacket，解码器产生一个 AVFrame
- **THEN** 输出端口 Push 一个 payload 为 MediaFrame 的 MediaBuffer，含 PTS 和 MediaType::kVideo

#### Scenario: Drop frames until seek target
- **WHEN** SetDropUntilPts(10.0) 后解码器输出 PTS=8.0 的非参考帧
- **THEN** 帧被丢弃不输出；PTS=10.0 的帧正常输出

#### Scenario: Decoder handles hardware acceleration
- **WHEN** Configure 时传入有效的 HWAccelContext（D3D11VA）
- **THEN** 解码器打开 D3D11 codec，输出帧 pixel_format 为 D3D11，零拷贝传递

#### Scenario: EOS propagates through decoder
- **WHEN** 输入端口收到 flags=kEos 的 MediaBuffer
- **THEN** 调用 avcodec_send_packet(nullptr) 冲刷解码器缓冲，最后输出 kEos 的 MediaBuffer
