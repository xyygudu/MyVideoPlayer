## MODIFIED Requirements

### Requirement: Renderer notifies Player on EOF
VideoSinkNode 与 AudioSinkNode SHALL 在拉到当前世代的 EOS buffer、且之前的数据都已呈现/送入设备后，向图的消息总线投递 `kEos`（携带该世代 `serial`）。MediaPlayer 的事件线程 SHALL 按世代汇总：同一世代内所有活跃 sink 都报告后才转为 kFinished；旧世代的 EOS SHALL 被忽略。

#### Scenario: Video EOF alone does not finish (if audio exists)
- **WHEN** 视频流报告 EOF 但音频流仍在播放
- **THEN** 状态保持 Playing

#### Scenario: All streams EOF triggers Finished
- **WHEN** 所有活跃流（音频+视频，或仅视频）均报告 EOF
- **THEN** 状态转为 Finished

#### Scenario: EOS from before a seek is ignored
- **WHEN** 视频 sink 报告 EOS 后、音频 sink 报告之前用户 seek
- **THEN** 已收到的 EOS 作废，新位置播放到结尾时重新汇总

### Requirement: Player fires playback finished callback
MediaPlayer SHALL 提供 `SetPlaybackFinishedCallback(std::function<void()>)` 接口。每次播放到结尾（转为 kFinished）时 SHALL 调用该回调一次。回调 SHALL 在内部事件线程、facade 锁之外执行：其中可以调用 Play/Pause/Seek 与查询接口，`Open()`/`Close()` SHALL 被拒绝并记录错误日志（它们需要 join 事件线程本身）。

#### Scenario: Callback invoked on playback end
- **WHEN** 播放到文件末尾，状态转为 Finished
- **THEN** PlaybackFinished 回调被调用一次

#### Scenario: No callback if not registered
- **WHEN** 未注册回调，播放到文件末尾
- **THEN** 状态正常转为 Finished，无崩溃

#### Scenario: Close from the callback is refused
- **WHEN** 回调中调用 `Close()`
- **THEN** 调用被忽略并记录错误，事件线程不会 join 自身
