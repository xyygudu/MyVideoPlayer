## ADDED Requirements

### Requirement: Audio clock tracks playback position
系统 SHALL 维护一个音频时钟（`audio_clock`），在 SDL 音频回调每次输出采样数据时更新为当前音频播放时间戳。

#### Scenario: Audio clock updates during playback
- **WHEN** SDL 音频回调输出了一段 PCM 数据
- **THEN** `audio_clock` 更新为对应的 PTS 值（秒），精度不低于毫秒级

### Requirement: Video frame display syncs to audio clock
系统 SHALL 在视频渲染循环中将视频帧的 PTS 与 `audio_clock` 对比，决定显示时机。

#### Scenario: Video frame PTS <= audio clock
- **WHEN** 视频帧的 PTS 小于等于当前 `audio_clock`
- **THEN** 系统立即显示该帧

#### Scenario: Video frame PTS > audio clock
- **WHEN** 视频帧的 PTS 大于当前 `audio_clock`
- **THEN** 系统等待差值时间后再显示该帧

#### Scenario: Video frame is too late
- **WHEN** 视频帧的 PTS 远小于 `audio_clock`（差值超过同步阈值，如 100ms）
- **THEN** 系统丢弃该帧，取下一帧继续同步判断
