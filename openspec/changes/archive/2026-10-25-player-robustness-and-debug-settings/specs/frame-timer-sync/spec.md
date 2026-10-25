## MODIFIED Requirements

### Requirement: frame_timer accumulates corrected delay each frame
从钟模式下（主时钟由其他节点提供），每帧显示前 SHALL 计算修正后的 delay 并累积到 frame_timer_（与 ffplay `compute_target_delay` 一致）：
1. `delay = pts - last_pts`（屏幕帧的显示时长；越界时取 1/帧率）
2. `diff = VideoClock()->Get() - MasterClock()->Get()`：视频时钟是屏幕帧的 pts 外推到当前时刻，而不是下一帧的 pts
3. `sync_threshold = clamp(delay, kSyncThresholdMin, kSyncThresholdMax)`
4. 若 `|diff| >= kMaxFrameDuration`（10 秒）：视为时间戳跳变，不做修正
5. 若 `diff <= -sync_threshold`：`delay = max(0, delay + diff)`（视频落后，追赶）
6. 若 `diff >= sync_threshold`：
   - 帧间隔 > kSyncThresholdMax（低帧率）：`delay += diff`（一次修正）
   - 帧间隔 ≤ kSyncThresholdMax（高帧率）：`delay = 2 * delay`（分散修正）
7. `frame_timer_ += delay`

#### Scenario: Normal playback accumulates frame interval
- **WHEN** 25fps 视频正常播放，diff 在 sync_threshold 范围内
- **THEN** frame_timer_ 每帧增加约 40ms

#### Scenario: High framerate video ahead uses 2x delay
- **WHEN** 60fps 视频（delay≈16ms），视频时钟比主时钟超前 80ms（超过 sync_threshold 40ms）
- **THEN** delay = 2 × 16ms = 32ms，frame_timer_ 增加 32ms（分散修正，约 80/16 ≈ 5 帧追平）

#### Scenario: Low framerate video ahead uses delay+diff
- **WHEN** 5fps 视频（delay=200ms），视频时钟比主时钟超前 150ms（超过 sync_threshold 100ms）
- **THEN** delay = 200ms + 150ms = 350ms，frame_timer_ 增加 350ms（一次修正）

#### Scenario: Video behind catches up
- **WHEN** 25fps 视频的视频时钟比主时钟落后 200ms（超过 sync_threshold）
- **THEN** delay = max(0, 40ms − 200ms) = 0，frame_timer_ 不推进

#### Scenario: Timestamp jump is not corrected
- **WHEN** diff 达到 12 秒（时间戳跳变）
- **THEN** 不修正，delay 保持帧间隔

### Requirement: actual_wait derived from frame_timer vs wall-clock
系统 SHALL 计算 `actual_wait = frame_timer_ - Clock::Now()`。若 actual_wait > 0，SHALL 一直等到该时刻：等待按 kMaxSleepSeconds 分片，以便及时响应暂停、seek 与停止，但总等待时长 SHALL NOT 被封顶（2fps 视频每帧要等 0.5 秒）；若 actual_wait 在 [负阈值, 0] 范围内，立即显示。

#### Scenario: Positive actual_wait triggers sleep
- **WHEN** frame_timer_ = 1040ms, Clock::Now() = 1000ms
- **THEN** sleep 40ms

#### Scenario: Small negative actual_wait displays immediately
- **WHEN** frame_timer_ = 998ms, Clock::Now() = 1000ms
- **THEN** 立即显示（不 sleep）

#### Scenario: Long frames wait their full duration
- **WHEN** 2fps 视频跟随音频播放
- **THEN** 每帧在屏幕上停留约 0.5 秒，屏幕帧 pts 不超前主时钟超过一个同步阈值

#### Scenario: Pause interrupts a long wait
- **WHEN** 等待 0.5 秒的过程中用户暂停
- **THEN** 至多 kMaxSleepSeconds 后停止等待，该帧在恢复播放后显示
