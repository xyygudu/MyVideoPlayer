## MODIFIED Requirements

### Requirement: Nodes declare port caps and graph validates compatibility
VideoFormat SHALL 携带 `hw_sw_format` 字段:硬件域帧底下实际软件布局(kNV12 等),软件帧为 kUnknown。

硬件帧域(kD3D11 等)与软格式同处 `pixel_formats` 维度:节点声明可生产/接受的帧域,兼容性判断规则不变(双方同时约束且无交集才冲突)。帧域不在 caps 中单独成维度,也不采用泛化的"hardware"取值——域必须精确到设备,以便将来协商跨设备映射。

#### Scenario: 硬件域参与交集判断
- **WHEN** 上游 caps 声明 {kD3D11, kYUV420P} 而下游声明 {kYUV420P}
- **THEN** 两 caps 兼容(有交集),上游协商时选择 kYUV420P

#### Scenario: 域冲突
- **WHEN** 上游仅声明 {kD3D11} 而下游仅声明 {kNV12}
- **THEN** ValidateCaps 判定不兼容,协商失败
