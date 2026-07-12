## ADDED Requirements

### Requirement: Effect info query
系统 SHALL 提供 `MediaPlayer::EffectInfos() const`，委托给内部 `EffectManager::Describe()`，返回当前 graph 中所有已注册特效节点的描述列表（`std::vector<EffectInfo>`），每项包含 `effect_id`、`display_name`、`enabled` 和该节点的 `params`（`std::vector<EffectParam>`，含每个参数的类型与当前值）。未 `Open()` 或 graph 未构建时 SHALL 返回空列表。

#### Scenario: 打开视频后查询特效信息
- **WHEN** `Open()` 成功后调用 `EffectInfos()`
- **THEN** 返回包含 "transform" 和 "color" 两个 EffectInfo 的列表，每项 `enabled` 默认为 true

#### Scenario: 未打开源时查询返回空列表
- **WHEN** 尚未调用 `Open()` 或已 `Close()` 后调用 `EffectInfos()`
- **THEN** 返回空的 vector，不抛异常

### Requirement: Effect parameter control
系统 SHALL 提供 `MediaPlayer::SetEffectParam(const std::string& effect_id, const std::string& param_id, EffectParamValue value)`，委托给内部 `EffectManager::SetParam()`，将参数值路由到 graph 中对应 `effect_id` 的 `IEffectNode::SetParam()`。若 `effect_id` 不存在，SHALL 记录 spdlog 警告并忽略该调用，不崩溃。

#### Scenario: 设置已知特效的参数
- **WHEN** 调用 `SetEffectParam("color", "brightness", 0.3f)`，且 graph 中存在 effect_id 为 "color" 的节点
- **THEN** 该节点的 `brightness` 参数被设置为 0.3f，后续处理帧体现该变化

#### Scenario: 设置不存在的 effect_id
- **WHEN** 调用 `SetEffectParam("nonexistent", "x", 1.0f)`
- **THEN** 记录警告日志，不影响其他节点状态，不崩溃

### Requirement: Effect enable/disable control
系统 SHALL 提供 `MediaPlayer::SetEffectEnabled(const std::string& effect_id, bool enabled)`，委托给内部 `EffectManager::SetEnabled()`，将启用状态路由到对应 `effect_id` 的 `IEffectNode::SetEnabled()`。若 `effect_id` 不存在，SHALL 记录 spdlog 警告并忽略该调用，不崩溃。

#### Scenario: 禁用已知特效
- **WHEN** 调用 `SetEffectEnabled("transform", false)`，且 graph 中存在 effect_id 为 "transform" 的节点
- **THEN** 该节点后续 `Process()` 直通输入 buffer，不做任何几何变换
