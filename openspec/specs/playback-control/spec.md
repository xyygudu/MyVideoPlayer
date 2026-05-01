## ADDED Requirements

### Requirement: Play starts playback
系统 SHALL 提供 `Play()` 接口，调用后启动 demux 线程、解码线程和音频输出，开始播放。

#### Scenario: Start playback from paused state
- **WHEN** 文件已 Open 且当前为暂停状态，调用 `Play()`
- **THEN** 音视频恢复播放，`IsPlaying()` 返回 true

#### Scenario: Play on already playing does nothing
- **WHEN** 当前已在播放状态，再次调用 `Play()`
- **THEN** 无副作用，继续播放

### Requirement: Pause stops playback
系统 SHALL 提供 `Pause()` 接口，调用后暂停音视频输出，保持当前位置。

#### Scenario: Pause during playback
- **WHEN** 当前正在播放，调用 `Pause()`
- **THEN** 音频输出暂停，视频画面冻结在当前帧，`IsPlaying()` 返回 false

### Requirement: Seek jumps to target position
系统 SHALL 提供 `Seek(double position_seconds)` 接口，跳转到指定时间位置。

#### Scenario: Seek to a valid position
- **WHEN** 调用 `Seek(30.0)` 且视频时长大于 30 秒
- **THEN** 系统调用 `av_seek_frame` 跳转，清空所有队列，flush 解码器，从新位置继续播放

#### Scenario: Seek while paused
- **WHEN** 当前为暂停状态，调用 `Seek(10.0)`
- **THEN** 系统跳转到目标位置，显示该位置的视频帧，保持暂停状态

### Requirement: Duration and position queries
系统 SHALL 提供 `Duration()`、`CurrentPosition()`、`CurrentVideoPosition()` 和 `VideoFps()` 接口。
- `Duration()` 返回视频总时长（秒）
- `CurrentPosition()` 返回当前音频时钟值（秒），用于音视频同步和时间显示
- `CurrentVideoPosition()` 返回当前屏幕显示帧的精确 PTS（秒），用于进度条和帧号显示
- `VideoFps()` 返回视频流的平均帧率

#### Scenario: Query duration after open
- **WHEN** 文件已成功 Open
- **THEN** `Duration()` 返回大于 0 的秒数值

#### Scenario: Query current position during playback
- **WHEN** 正在播放
- **THEN** `CurrentPosition()` 返回当前音频时钟值，随播放推进而增长

#### Scenario: Query video position during playback
- **WHEN** 正在播放
- **THEN** `CurrentVideoPosition()` 返回最后渲染帧的 PTS 值

#### Scenario: Query video FPS
- **WHEN** 文件已成功 Open 且含视频流
- **THEN** `VideoFps()` 返回正确帧率值

### Requirement: Close releases resources
系统 SHALL 提供 `Close()` 接口，停止所有线程，释放 FFmpeg 和 SDL 资源。

#### Scenario: Close after playback
- **WHEN** 调用 `Close()`
- **THEN** 所有线程停止，队列清空，FFmpeg context 和 SDL 音频设备关闭，可安全再次 Open 新文件
