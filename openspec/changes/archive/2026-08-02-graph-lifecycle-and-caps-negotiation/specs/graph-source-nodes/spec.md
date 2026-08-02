## MODIFIED Requirements

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
