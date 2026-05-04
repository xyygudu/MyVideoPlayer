## MODIFIED Requirements

### Requirement: Video frame display syncs to audio clock
系统 SHALL 在 AudioMaster 模式下，使用 frame_timer 累积校正算法决定视频帧显示时机。具体流程：
1. 计算帧间隔 delay 和音频偏差 diff
2. 用自适应 sync_threshold 判定是否修正 delay
3. 累积 delay 到 frame_timer_
4. 用 frame_timer_ 与 wall-clock 的差值决定 sleep/display/reset

不再直接用 diff 值作为 sleep 时长。丢帧判定改为 frame_timer 重置机制自然覆盖（而非独立的 kDropThreshold 比较）。

#### Scenario: Video frame within sync tolerance
- **WHEN** AudioMaster 模式，video_pts 与 audio_clock 差值在 [-sync_threshold, sync_threshold] 范围内
- **THEN** delay 不修正，frame_timer_ 按正常帧间隔推进

#### Scenario: Video frame ahead of audio beyond threshold
- **WHEN** AudioMaster 模式，video_pts - audio_clock > sync_threshold
- **THEN** delay += diff，frame_timer_ 多推进，actual_wait 增大（等待音频追上）

#### Scenario: Video frame behind audio beyond threshold
- **WHEN** AudioMaster 模式，audio_clock - video_pts > sync_threshold
- **THEN** delay = 0，frame_timer_ 不推进，actual_wait ≤ 0，立即显示

#### Scenario: Large discontinuity resets frame_timer
- **WHEN** AudioMaster 模式，frame_timer_ - Clock::Now() < -kMaxSleepSeconds
- **THEN** frame_timer_ 重置为 Clock::Now()，当前帧立即显示（不丢弃）
