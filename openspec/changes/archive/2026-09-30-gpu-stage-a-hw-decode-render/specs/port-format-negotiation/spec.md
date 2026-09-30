## MODIFIED Requirements

### Requirement: Nodes declare port caps and graph validates compatibility
节点 SHALL 在 `DeclareCaps()` 中通过 `InputPort::SetCaps()` 声明本端口可接受的格式与需求。`FormatCaps` 中留空的维度 SHALL 表示“无约束”，不参与兼容性判定。

MediaGraph SHALL 在 `DeclareCaps()` 之后、`Negotiate()` 之前对每条连接执行 `FormatCaps::Compatible()` 校验；某一维度被双方同时约束且无交集 SHALL 判定为不兼容并使协商失败。该校验 SHALL NOT 在 `OutputPort::Connect()` 中执行——Connect 发生于建图期，早于 caps 声明。

上游节点 SHALL 通过 `output_port_->Peer()->Caps()` 读取下游需求，据此决定自身输出格式（对应 "downstream suggests, upstream decides"）。

VideoFormat SHALL 携带 `hw_sw_format` 字段:硬件域帧底下实际软件布局(kNV12 等),软件帧为 kUnknown。

硬件帧域(kD3D11 等)与软格式同处 `pixel_formats` 维度:节点声明可生产/接受的帧域,兼容性判断规则不变(双方同时约束且无交集才冲突)。帧域不在 caps 中单独成维度,也不采用泛化的"hardware"取值——域必须精确到设备,以便将来协商跨设备映射。

#### Scenario: Empty caps means no constraint
- **WHEN** 某端口未声明任何 caps
- **THEN** 与任意上游格式均判定为兼容

#### Scenario: Incompatible caps fail negotiation
- **WHEN** 上下游端口的 FormatCaps 存在相互矛盾的维度
- **THEN** MediaGraph::Negotiate() 返回 false 并记录不匹配的连接

#### Scenario: Upstream reads downstream caps during negotiation
- **WHEN** EncoderNode::Negotiate() 执行
- **THEN** 可通过 output_port_->Peer()->Caps() 读到下游 MuxNode 声明的需求

#### Scenario: 硬件域参与交集判断
- **WHEN** 上游 caps 声明 {kD3D11, kYUV420P} 而下游声明 {kYUV420P}
- **THEN** 两 caps 兼容(有交集),上游协商时选择 kYUV420P

#### Scenario: 域冲突
- **WHEN** 上游仅声明 {kD3D11} 而下游仅声明 {kNV12}
- **THEN** ValidateCaps 判定不兼容,协商失败
