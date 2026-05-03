## MODIFIED Requirements

### Requirement: Play starts playback
系统 SHALL 提供 `Play()` 接口。当状态为 Ready 或 Paused 时，调用后启动/恢复 demux 线程、解码线程和音频输出，TransitionTo(Playing)。

#### Scenario: Start playback from Ready state
- **WHEN** 状态为 Ready，调用 Play()
- **THEN** 所有线程启动，状态转为 Playing

#### Scenario: Resume from Paused state
- **WHEN** 状态为 Paused，调用 Play()
- **THEN** Clock 恢复推进，音视频恢复播放，状态转为 Playing

#### Scenario: Play on already playing does nothing
- **WHEN** 状态为 Playing，再次调用 Play()
- **THEN** 无副作用，继续播放

#### Scenario: Play from Finished state restarts
- **WHEN** 状态为 Finished，调用 Play()
- **THEN** Seek 到开头并开始播放，状态转为 Playing

### Requirement: Pause stops playback
系统 SHALL 提供 `Pause()` 接口。当状态为 Playing 时，暂停音视频输出、冻结 Clock，TransitionTo(Paused)。

#### Scenario: Pause during playback
- **WHEN** 状态为 Playing，调用 Pause()
- **THEN** Clock 冻结，音频输出暂停，视频画面冻结，状态转为 Paused

#### Scenario: Pause when not playing is ignored
- **WHEN** 状态为 Paused 或 Idle，调用 Pause()
- **THEN** 无操作

### Requirement: Seek jumps to target position
系统 SHALL 提供 `Seek(double position_seconds)` 接口。Seek SHALL：
1. 对所有 StreamContext 调用 Flush()
2. 清除 AudioRenderer 的 SDL 缓冲
3. 设置 seek_target_ 用于帧精确丢弃
4. 调用 demuxer_.RequestSeek()
5. 重设 master clock 到目标位置

#### Scenario: Seek to a valid position
- **WHEN** 调用 Seek(30.0) 且视频时长大于 30 秒
- **THEN** 队列被 Flush，demuxer seek，从新位置继续，clock 重设为 30.0

#### Scenario: Seek while paused shows target frame
- **WHEN** 状态为 Paused，调用 Seek(10.0)
- **THEN** 跳转到目标位置，显示 PTS >= 10.0 的第一帧，保持 Paused

#### Scenario: Frame-accurate seek discards pre-target frames
- **WHEN** Seek(10.0) 后 Decoder 输出 PTS=9.8 的帧
- **THEN** VideoRenderLoop 丢弃该帧，继续等待 PTS >= 10.0 的帧

### Requirement: Duration and position queries
系统 SHALL 提供以下查询接口：
- `Duration()` 返回视频总时长（秒）
- `CurrentPosition()` 返回 MasterClock().Get() 的值（基于 wall-time 外推，平滑推进）
- `CurrentVideoPosition()` 返回当前屏幕显示帧的精确 PTS（秒）
- `VideoFps()` 返回视频流的平均帧率
- `State()` 返回当前 PlayerState 枚举值

#### Scenario: CurrentPosition advances smoothly
- **WHEN** 正在播放，连续两次调用 CurrentPosition() 间隔 16ms
- **THEN** 两次返回值之差约为 0.016（非阶梯跳变）

#### Scenario: CurrentPosition works without audio
- **WHEN** 打开纯视频文件并播放
- **THEN** CurrentPosition() 基于 video_clock_ 返回平滑推进的值

#### Scenario: Query duration after open
- **WHEN** 文件已成功 Open
- **THEN** Duration() 返回大于 0 的秒数值

### Requirement: Close releases resources
系统 SHALL 提供 `Close()` 接口。Close SHALL 对所有 StreamContext 调用 Abort()，停止所有线程，释放 FFmpeg 和 SDL 资源，TransitionTo(Idle)。

#### Scenario: Close after playback
- **WHEN** 调用 Close()
- **THEN** 所有线程停止，资源释放，状态转为 Idle，可安全再次 Open 新文件

### Requirement: StepFrame advances one frame while paused
系统 SHALL 提供 `StepFrame()` 接口。仅在 Paused 状态下有效，使用条件变量通知 VideoRenderLoop 渲染下一帧。

#### Scenario: Step shows next frame
- **WHEN** 状态为 Paused，调用 StepFrame()
- **THEN** 下一帧被渲染显示，CurrentVideoPosition() 更新，状态保持 Paused

### Requirement: Playback finished callback
系统 SHALL 提供 `SetPlaybackFinishedCallback(std::function<void()>)` 接口。当状态转为 Finished 时触发回调。

#### Scenario: Callback fires on EOF
- **WHEN** 所有流 EOF，状态转为 Finished
- **THEN** 注册的回调被调用一次
