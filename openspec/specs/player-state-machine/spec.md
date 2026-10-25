## Purpose

Defines who owns the playback state and how it changes.
## Requirements
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

