## ADDED Requirements

### Requirement: Video PTS tracking
系统 SHALL 在 `PlayerImpl` 中维护 `std::atomic<double> video_pts_`，记录最后渲染到屏幕的视频帧的 PTS（秒）。

#### Scenario: PTS updated after each frame render
- **WHEN** VideoRenderLoop 完成一帧的渲染回调
- **THEN** `video_pts_` 被更新为该帧的 PTS 值

#### Scenario: PTS reset on seek
- **WHEN** 用户执行 Seek 操作后第一帧渲染完成
- **THEN** `video_pts_` 反映新位置的帧 PTS

#### Scenario: PTS initial value
- **WHEN** 文件 Open 完成但尚未开始播放
- **THEN** `video_pts_` 为 0.0

### Requirement: CurrentVideoPosition API
系统 SHALL 提供 `Player::CurrentVideoPosition()` 接口，返回当前屏幕显示帧的精确时间位置（秒）。

#### Scenario: Read position during playback
- **WHEN** 视频正在播放
- **THEN** `CurrentVideoPosition()` 返回最后渲染帧的 PTS，与屏幕画面一致

#### Scenario: Read position while paused
- **WHEN** 视频暂停
- **THEN** `CurrentVideoPosition()` 返回暂停时最后渲染帧的 PTS

### Requirement: VideoFps API
系统 SHALL 提供 `Player::VideoFps()` 接口，返回视频流的平均帧率。

#### Scenario: Query FPS after open
- **WHEN** 文件已成功 Open
- **THEN** `VideoFps()` 返回 `avg_frame_rate` 计算得到的 double 值（如 29.97, 25.0, 60.0）

#### Scenario: Query FPS with no video stream
- **WHEN** 文件仅包含音频流
- **THEN** `VideoFps()` 返回 0.0

### Requirement: UI frame position display
UI SHALL 在进度条区域显示当前帧号和总帧数，格式为 `Frame <current> / <total>`。

#### Scenario: Display frame info during playback
- **WHEN** QTimer tick 且视频正在播放
- **THEN** UI 读取 `CurrentVideoPosition()` 和 `VideoFps()`，计算 frame_number = pts × fps，显示 `Frame 74 / 2700`

#### Scenario: Display frame info for audio-only file
- **WHEN** 文件无视频流
- **THEN** 帧号显示区域隐藏或显示 N/A
