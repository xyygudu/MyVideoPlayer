## ADDED Requirements

### Requirement: Nodes declare port caps and graph validates compatibility
节点 SHALL 在 `DeclareCaps()` 中通过 `InputPort::SetCaps()` 声明本端口可接受的格式与需求。`FormatCaps` 中留空的维度 SHALL 表示"无约束"，不参与兼容性判定。

MediaGraph SHALL 在 `DeclareCaps()` 之后、`Negotiate()` 之前对每条连接执行 `FormatCaps::Compatible()` 校验；某一维度被双方同时约束且无交集 SHALL 判定为不兼容并使协商失败。该校验 SHALL NOT 在 `OutputPort::Connect()` 中执行——Connect 发生于建图期，早于 caps 声明。

上游节点 SHALL 通过 `output_port_->Peer()->Caps()` 读取下游需求，据此决定自身输出格式（对应 "downstream suggests, upstream decides"）。

#### Scenario: Empty caps means no constraint
- **WHEN** 某端口未声明任何 caps
- **THEN** 与任意上游格式均判定为兼容

#### Scenario: Incompatible caps fail negotiation
- **WHEN** 上下游端口的 FormatCaps 交集为空
- **THEN** MediaGraph::Negotiate() 返回 false 并记录不匹配的具体维度

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

## REMOVED Requirements

### Requirement: InputPort carries downstream needs_global_header requirement
**Reason**: 该字段是 caps 机制缺位时的临时通道，现由 `FormatCaps::HeaderPlacement` 统一承载，避免出现第二套并行的需求传递机制。
**Migration**: MuxNode 改为在 `DeclareCaps()` 中声明 `HeaderPlacement`；EncoderNode 改为在 `Negotiate()` 中通过 `output_port_->Peer()->Caps()` 读取。`InputPort::SetNeedsGlobalHeader/NeedsGlobalHeader` 删除。
