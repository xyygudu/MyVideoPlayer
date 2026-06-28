## MODIFIED Requirements

### Requirement: INode interface removes Configure(NodeConfig)
INode 接口 SHALL 移除 `virtual bool Configure(const NodeConfig& config)` 方法。`NodeConfig` 结构体 SHALL 被删除。

各节点的配置 SHALL 改为以下方式之一：
- 构造参数注入（如 `DemuxNode(std::string file_path)`）
- 节点专属的 setter 方法（已有的 SetRenderer/SetAudioClock 等模式）

INode 接口 SHALL 保留：Negotiate / Prepare / Start / Stop / Flush / Process / Inputs / Outputs / Type / Threading / State / Name。

#### Scenario: DemuxNode configured via constructor
- **WHEN** MediaPlayer 创建 DemuxNode
- **THEN** 使用 `std::make_unique<DemuxNode>(filepath)` 构造注入路径

#### Scenario: INode has no Configure method
- **WHEN** MediaGraph 管理节点生命周期
- **THEN** 仅调用 Negotiate/Prepare/Start/Stop/Flush，不调用 Configure

### Requirement: MediaFormat extends with codec_params field
MediaFormat SHALL 新增 `std::shared_ptr<AVCodecParameters> codec_params_` 私有成员和 `codec_params()` 访问器。现有字段（width/height/pixel_format/sample_rate/channels 等）保留不变。

#### Scenario: Existing format fields unchanged
- **WHEN** 使用 MediaFormat::Video() 或 MediaFormat::Audio() 工厂
- **THEN** 行为与重构前完全一致

#### Scenario: FromStream factory fills all fields
- **WHEN** 使用 MediaFormat::FromStream() 从 AVCodecParameters 构造
- **THEN** codec_params_ 携带深拷贝，同时 codec_id/width/height/sample_rate 等字段也被填充

### Requirement: NodeState kConfigured semantics adjustment
NodeState::kConfigured SHALL 保留在枚举中，语义调整为「构造完成、可接收 Negotiate/Prepare」。Prepare() SHALL 同时接受 kIdle 和 kConfigured 两种入口状态。

#### Scenario: Prepare accepts Idle state
- **WHEN** 节点从 kIdle 直接调用 Prepare()（已通过构造完成配置）
- **THEN** Prepare() 执行成功，状态转为 kPrepared
