## Requirements

### Requirement: GraphState replaces PlayerState
播放状态管理 SHALL 从 `PlayerState`（Idle/Ready/Playing/Paused/Finished）迁移到 `GraphState` 枚举，由 MediaGraph 统一管理。

GraphState SHALL 包含：`kIdle, kReady, kPlaying, kPaused, kFinished, kError`。状态转换逻辑 SHALL 从 PlayerImpl::TransitionTo() 迁移到 MediaGraph 内部。

MediaPlayer 暴露的 public API SHALL 保持 State() 方法，内部代理到 MediaGraph::State()。

#### Scenario: Graph state transitions mirror old behavior
- **WHEN** MediaPlayer::Play() 调用 MediaGraph::Start()
- **THEN** GraphState 从 kReady 转为 kPlaying

#### Scenario: Error state added
- **WHEN** 任何一个节点进入 Error 状态
- **THEN** GraphState 转为 kError，EventCallback 收到 kError 事件

### Requirement: StepFrame is a Paused-state graph action
StepFrame() 行为 SHALL 保留，但实现方式从 PlayerImpl 的条件变量通知改为 VideoSinkNode 的帧步进方法。

#### Scenario: Step frame in graph mode
- **WHEN** Graph 处于 kPaused，调用 StepFrame()
- **THEN** VideoSinkNode 渲染下一帧并更新显示，Graph 保持 kPaused
