## MODIFIED Requirements

### Requirement: PlaybackGraphBuilder 封装播放图构建
系统 SHALL 不引入独立的 `PlaybackGraphBuilder` 类或 `PlaybackContext` 结构体。

`MediaPlayer::Impl::BuildGraph` SHALL 作为私有方法直接实现播放图构建，通过预探帧消除数据依赖后函数体 ≤ 40 行。构建阶段为：创建 Graph → 创建节点（含 Setter 配置）→ inline Connect 连线（视频分支通过 `WireVideoEffects` 辅助方法插入特效节点）→ Negotiate + Prepare。

视频分支的连接顺序 SHALL 为：`DemuxNode -> DecoderNode -> TransformEffectNode -> ColorEffectNode -> VideoSinkNode`。`WireVideoEffects` 私有辅助方法 SHALL 封装"创建 TransformEffectNode/ColorEffectNode → AddNode → 内部 Connect(Decoder 输出, Transform 输入) 与 Connect(Transform 输出, Color 输入) → 调用 `effect_manager_.Register(effect_id, display_name, node)` 注册到 `EffectManager` → 返回 Color 的输出端口"，供 `BuildGraph` 将该输出端口 Connect 到 `VideoSinkNode` 输入。`EffectManager` 只负责按 effect_id 索引/查询/控制，不参与拓扑连接决策。

#### Scenario: BuildGraph 职责单一
- **WHEN** MediaPlayer::BuildGraph 重构后
- **THEN** 函数仅包含：new MediaGraph → AddNode（含 Setter 配置）→ Connect（视频分支通过 WireVideoEffects）→ Negotiate/Prepare
- **AND** 无指针森林，无分散的元数据提取
- **AND** 函数体不超过 40 行

#### Scenario: 视频分支按顺序插入特效节点
- **WHEN** 存在视频流（`video_stream_index_ >= 0`）
- **THEN** 调用 `WireVideoEffects` 后，DecoderNode 的输出端口连接到 TransformEffectNode 输入，TransformEffectNode 输出连接到 ColorEffectNode 输入，ColorEffectNode 输出连接到 VideoSinkNode 输入
- **AND** 无视频流时不创建任何特效节点，不调用 `EffectManager::Register`

#### Scenario: EffectManager 注册支持后续参数路由
- **WHEN** `WireVideoEffects` 执行完毕
- **THEN** `MediaPlayer::Impl` 持有的 `EffectManager` 成员中已注册 "transform" 与 "color" 两个 effect_id，供 `SetEffectParam`/`SetEffectEnabled`/`EffectInfos` 使用

#### Scenario: Close 清空 EffectManager 避免悬空指针
- **WHEN** `MediaPlayer::Impl::Close()` 执行
- **THEN** 先调用 `effect_manager_.Clear()` 再 `graph_.reset()`，确保 EffectManager 不残留指向已释放节点的指针
