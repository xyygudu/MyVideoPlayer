## ADDED Requirements

### Requirement: Player uses PlayerState enum
PlayerImpl SHALL 使用 `enum class PlayerState { Idle, Ready, Playing, Paused, Finished }` 管理播放状态，替代多个 `atomic<bool>` 标志组合。

#### Scenario: Initial state is Idle
- **WHEN** PlayerImpl 构造完成
- **THEN** 状态为 PlayerState::Idle

#### Scenario: Open transitions to Ready
- **WHEN** 调用 Open() 成功
- **THEN** 状态从 Idle 转为 Ready

#### Scenario: Play transitions to Playing
- **WHEN** 状态为 Ready 或 Paused，调用 Play()
- **THEN** 状态转为 Playing

#### Scenario: Pause transitions to Paused
- **WHEN** 状态为 Playing，调用 Pause()
- **THEN** 状态转为 Paused

#### Scenario: Close transitions to Idle
- **WHEN** 调用 Close()
- **THEN** 状态转为 Idle，无论之前是什么状态

#### Scenario: EOF transitions to Finished
- **WHEN** 播放到达文件末尾
- **THEN** 状态转为 Finished

### Requirement: State transitions are atomic
PlayerImpl SHALL 使用 `std::atomic<PlayerState>` 存储状态。TransitionTo() SHALL 使用 compare_exchange 确保只允许合法的状态转换。

#### Scenario: Invalid transition is rejected
- **WHEN** 当前状态为 Idle，尝试 TransitionTo(Playing)
- **THEN** 转换被拒绝，状态保持 Idle，记录警告日志

#### Scenario: Concurrent state read is safe
- **WHEN** UI 线程调用 State() 同时 render 线程调用 TransitionTo()
- **THEN** 无数据竞争，UI 线程读到转换前或转换后的值

### Requirement: StepFrame is a Paused-state action
PlayerImpl SHALL 提供 `StepFrame()` 方法。StepFrame() 仅在 Paused 状态下有效，通知 VideoRenderLoop 渲染并显示下一帧，随后继续保持 Paused 状态。SHALL 使用条件变量（非轮询）通知渲染线程。

#### Scenario: Step while paused shows next frame
- **WHEN** 状态为 Paused，调用 StepFrame()
- **THEN** VideoRenderLoop 渲染下一帧并更新显示，状态保持 Paused

#### Scenario: Step while playing is ignored
- **WHEN** 状态为 Playing，调用 StepFrame()
- **THEN** 无操作

### Requirement: Player exposes State query
Player 公共 API SHALL 提供 `PlayerState State() const` 方法，返回当前播放状态。

#### Scenario: Query state from UI thread
- **WHEN** UI 线程调用 State()
- **THEN** 返回当前 PlayerState 枚举值
