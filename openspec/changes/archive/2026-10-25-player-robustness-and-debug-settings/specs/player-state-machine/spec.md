## ADDED Requirements

### Requirement: MediaPlayer 持有播放状态
播放状态 SHALL 是 `MediaPlayer` 的 `PlaybackState{kIdle, kReady, kPlaying, kPaused, kFinished, kError}`，由 facade 内部一把互斥锁保护。公共方法（任意线程）与内部事件线程是仅有的两类写者：前者处理用户操作，后者根据图消息总线推进 kFinished/kError 并执行设备丢失重建。

`State()` SHALL 随时可调用，并可能在两次调用之间因事件线程而变化。

#### Scenario: Play 从 Ready 进入 Playing
- **WHEN** `Open()` 成功后调用 `Play()`
- **THEN** 图被启动，`State()` 返回 kPlaying

#### Scenario: 运行期错误进入 kError
- **WHEN** 任一节点投递 kError
- **THEN** 事件线程暂停图（冻结输出）并将状态置为 kError，直到 `Close()` 或下一次 `Open()`

#### Scenario: 播放完毕进入 kFinished
- **WHEN** 当前世代所有 sink 都报告了 EOS
- **THEN** 状态转为 kFinished；此时 `Play()` 从头重播

## REMOVED Requirements

### Requirement: GraphState replaces PlayerState
**Reason**: 状态从未真正由图管理；把状态放在图里会让节点线程回调应用层修改它，造成跨线程重入。
**Migration**: 使用 `MediaPlayer::State()`（`PlaybackState`）；节点的结束与错误经 `MediaGraph::Bus()` 上报，由 facade 事件线程转换状态。

### Requirement: StepFrame is a Paused-state graph action
**Reason**: `MediaPlayer` 没有 StepFrame 接口；暂停时的单帧显示由 seek 预览承担。
**Migration**: 暂停时调用 `Seek(目标位置)`。
