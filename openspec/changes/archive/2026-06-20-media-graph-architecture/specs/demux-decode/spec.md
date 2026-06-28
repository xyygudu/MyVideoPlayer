## MODIFIED Requirements

### Requirement: Demuxer is implemented as DemuxNode
Demuxer 的行为逻辑（avformat_open_input + av_read_frame → 路由到 PacketQueue）SHALL 保留，但封装方式从独立类 `Demuxer` 变更为实现 INode 接口的 `DemuxNode`（**Source 类型**，无输入端口）。DemuxNode 合并了旧 Demuxer 的文件打开和包读取两个职责。

DemuxNode 的生命周期 SHALL 遵循 INode 状态机（Configure → Negotiate → Prepare → Start → Stop），而非旧 Demuxer 的 Open/Close 模式。数据输出 SHALL 通过动态创建的 OutputPort（每个流一个端口），而非直接写入全局 PacketQueue 指针。

#### Scenario: DemuxNode creates output ports dynamically
- **WHEN** DemuxNode::Prepare() 发现文件含 3 个流（1 video, 1 audio, 1 subtitle）
- **THEN** 创建 3 个 OutputPort，每个端口绑定对应 stream_index

#### Scenario: Seek through DemuxNode
- **WHEN** MediaGraph::Flush() 被调用
- **THEN** DemuxNode 清空内部缓冲，avformat_seek_file() 到目标位置

### Requirement: Decoder is implemented as DecoderNode
解码器行为逻辑（avcodec_send_packet/receive_frame → MediaFrame）SHALL 保留，但封装方式从 `AVFrameDecoder : IDecoder` 变更为实现 INode 接口的 `DecoderNode`。

DecoderNode SHALL 保留硬件加速支持（D3D11VA）和 SetDropUntilPts() seek 优化。数据流从"回调输出到 FrameQueue"改为"OutputPort::Push() 到 Link"。

#### Scenario: DecoderNode outputs to port not callback
- **WHEN** 解码出一帧 MediaFrame
- **THEN** 通过 OutputPort::Push(MediaBuffer{frame}) 发送，不调用回调函数

#### Scenario: DecoderNode retains HW acceleration
- **WHEN** 配置 HWAccelContext（D3D11VA）
- **THEN** 解码器打开 D3D11 codec，行为与旧 AVFrameDecoder 一致
