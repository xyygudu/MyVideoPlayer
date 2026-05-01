## MODIFIED Requirements

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
