## Purpose

Defines the port-level format negotiation mechanism: MediaFormat carrying
AVCodecParameters as shared copies, OutputPort::Connect auto-propagating
format to downstream, and downstream nodes self-configuring from input
port format without external setters.

## Requirements

### Requirement: MediaFormat carries codec parameters as shared copy
MediaFormat SHALL 持有 `std::shared_ptr<AVCodecParameters>` 成员，代表该端口传输的流的编码参数。提供 `FromStream()` 工厂方法深拷贝构造，`codec_params()` 访问器。

#### Scenario: DemuxNode fills codec params on output port
- **WHEN** DemuxNode::Prepare() 发现视频流
- **THEN** 输出端口的 MediaFormat 通过 FromStream() 携带完整 AVCodecParameters 拷贝

#### Scenario: MediaFormat copy is cheap (shared_ptr)
- **WHEN** MediaFormat 被拷贝
- **THEN** 内部 codec_params_ 仅增加引用计数

### Requirement: OutputPort::Connect propagates format to downstream InputPort
OutputPort::Connect() SHALL 在建立连接后调用 `peer->SetFormat(format_)` 将上游输出格式传播到下游输入端口。

#### Scenario: Downstream reads format after connect
- **WHEN** DemuxNode OutputPort 连接到 DecoderNode InputPort
- **THEN** DecoderNode 的 InputPort::Format() 返回 DemuxNode 输出端口的 MediaFormat

### Requirement: Downstream node negotiates from input port format
下游节点的 Negotiate() SHALL 从 InputPort::Format() 读取上游格式并自我配置，不再需要外部手动设置参数。

#### Scenario: DecoderNode self-configures from port format
- **WHEN** MediaGraph::Negotiate() 按拓扑序调用 DecoderNode::Negotiate()
- **THEN** DecoderNode 从 input_port_->Format().codec_params() 获取编码参数

### Requirement: InputPort carries downstream needs_global_header requirement
InputPort SHALL 承载下游节点在协商期写入的 `needs_global_header` 需求，供上游节点在 Prepare 期读取。上游 Encoder 通过其输出端口连接的 peer（即该 InputPort）读取该值。

#### Scenario: MuxNode publishes requirement to upstream encoder
- **WHEN** `MuxNode::Negotiate()` 解析容器后对输入端口调用 `SetNeedsGlobalHeader(needs_global_header_)`
- **THEN** 上游 `EncoderNode` 在 `Prepare()` 通过 `output_port_->Peer()->NeedsGlobalHeader()` 读到该值

#### Scenario: Unset requirement defaults to false
- **WHEN** InputPort 尚未被写入该需求
- **THEN** `NeedsGlobalHeader()` 返回 false（编码器按带内头处理）
