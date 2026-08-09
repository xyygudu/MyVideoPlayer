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

### Requirement: Nodes declare port caps and graph validates compatibility
节点 SHALL 在 `DeclareCaps()` 中通过 `InputPort::SetCaps()` 声明本端口可接受的格式与需求。`FormatCaps` 中留空的维度 SHALL 表示“无约束”，不参与兼容性判定。

MediaGraph SHALL 在 `DeclareCaps()` 之后、`Negotiate()` 之前对每条连接执行 `FormatCaps::Compatible()` 校验；某一维度被双方同时约束且无交集 SHALL 判定为不兼容并使协商失败。该校验 SHALL NOT 在 `OutputPort::Connect()` 中执行——Connect 发生于建图期，早于 caps 声明。

上游节点 SHALL 通过 `output_port_->Peer()->Caps()` 读取下游需求，据此决定自身输出格式（对应 "downstream suggests, upstream decides"）。

#### Scenario: Empty caps means no constraint
- **WHEN** 某端口未声明任何 caps
- **THEN** 与任意上游格式均判定为兼容

#### Scenario: Incompatible caps fail negotiation
- **WHEN** 上下游端口的 FormatCaps 存在相互矛盾的维度
- **THEN** MediaGraph::Negotiate() 返回 false 并记录不匹配的连接

#### Scenario: Upstream reads downstream caps during negotiation
- **WHEN** EncoderNode::Negotiate() 执行
- **THEN** 可通过 output_port_->Peer()->Caps() 读到下游 MuxNode 声明的需求

### Requirement: FormatCaps carries header placement requirement
`FormatCaps` SHALL 包含 `HeaderPlacement` 维度，取值 `kAny`（默认，无约束）、`kGlobal`（要求编码器把参数集写入 extradata）、`kInBand`（要求参数集随码流内联）。

#### Scenario: Muxer declares global header requirement
- **WHEN** 输出容器带 `AVFMT_GLOBALHEADER`（如 matroska/mp4）
- **THEN** MuxNode 在 DeclareCaps() 中将输入端口的 HeaderPlacement 声明为 kGlobal

#### Scenario: Muxer declares in-band requirement
- **WHEN** 输出容器不带 `AVFMT_GLOBALHEADER`（如 mpegts/avi）
- **THEN** MuxNode 声明 HeaderPlacement 为 kInBand

### Requirement: FormatCaps carries codec compatibility constraint
`FormatCaps` SHALL 包含 `codec_ids` 维度（`std::vector<int>`，空表示无约束），用于声明某端口能产出或能接受的编码器集合。`FormatCaps::Compatible()` SHALL 在该维度双方都非空且交集为空时判定为不兼容，与其余维度使用相同的"双方都约束、且无交集才拒绝"规则；`FormatCaps::Intersect()` SHALL 对该维度取交集。

#### Scenario: 编码器把自身声明为单一约束
- **WHEN** EncoderNode::DeclareCaps() 解析出编码器
- **THEN** 其输出端口 caps 的 `codec_ids` 为该编码器 codec_id 组成的单元素集合

#### Scenario: 不兼容的编码器使协商失败
- **WHEN** 上游端口声明的 `codec_ids` 与下游端口声明的 `codec_ids` 无交集
- **THEN** `FormatCaps::Compatible()` 返回 false，`MediaGraph::Negotiate()` 在 `ValidateCaps()` 阶段失败，早于任何 `avcodec_open2`/`avformat_write_header` 调用

#### Scenario: 未声明 codec_ids 的端口接受任意编码器
- **WHEN** 某端口的 caps 未设置 `codec_ids`（保持默认空）
- **THEN** 该维度不参与兼容性判定，视为无约束
