### Requirement: frame_timer maintains absolute display timeline
系统 SHALL 维护一个 `frame_timer_` 成员（double，单位秒），表示当前帧理论上应在 wall-clock 何时显示。VideoRenderLoop 启动时 SHALL 初始化 `frame_timer_ = Clock::Now()`。

#### Scenario: frame_timer initialized on render loop start
- **WHEN** VideoRenderLoop 启动
- **THEN** frame_timer_ 被设置为当前 wall-clock 时间

### Requirement: frame_timer accumulates corrected delay each frame
从钟模式下（主时钟由其他节点提供），每帧显示前 SHALL 计算修正后的 delay 并累积到 frame_timer_：
1. `delay = pts - last_pts`（帧间隔）
2. `diff = pts - MasterClock()->Get()`（与主时钟的偏差）
3. `sync_threshold = clamp(delay, kSyncThresholdMin, kSyncThresholdMax)`
4. 若 `diff > sync_threshold`：
   - 帧间隔 > kSyncThresholdMax（低帧率）：`delay += diff`（一次修正）
   - 帧间隔 ≤ kSyncThresholdMax（高帧率）：`delay = 2 * delay`（分散修正）
5. 若 `diff < -sync_threshold`：`delay = 0`（视频落后，立即显示）
6. `frame_timer_ += delay`

#### Scenario: Normal playback accumulates frame interval
- **WHEN** 25fps 视频正常播放，diff 在 sync_threshold 范围内
- **THEN** frame_timer_ 每帧增加约 40ms

#### Scenario: High framerate video ahead uses 2x delay
- **WHEN** 60fps 视频（delay≈16ms），video_pts 比主时钟超前 80ms（超过 sync_threshold 40ms）
- **THEN** delay = 2 × 16ms = 32ms，frame_timer_ 增加 32ms（分散修正，约 80/16 ≈ 5 帧追平）

#### Scenario: Low framerate video ahead uses delay+diff
- **WHEN** 5fps 视频（delay=200ms），video_pts 比主时钟超前 150ms（超过 sync_threshold 100ms）
- **THEN** delay = 200ms + 150ms = 350ms，frame_timer_ 增加 350ms（一次修正）

#### Scenario: Video behind sets delay to zero
- **WHEN** video_pts 比主时钟落后 200ms（超过 sync_threshold）
- **THEN** delay = 0，frame_timer_ 不推进

### Requirement: actual_wait derived from frame_timer vs wall-clock
系统 SHALL 计算 `actual_wait = frame_timer_ - Clock::Now()`。若 actual_wait > 0，sleep(actual_wait)（上限 kMaxSleepSeconds）；若 actual_wait 在 [负阈值, 0] 范围内，立即显示。

#### Scenario: Positive actual_wait triggers sleep
- **WHEN** frame_timer_ = 1040ms, Clock::Now() = 1000ms
- **THEN** sleep 40ms

#### Scenario: Small negative actual_wait displays immediately
- **WHEN** frame_timer_ = 998ms, Clock::Now() = 1000ms
- **THEN** 立即显示（不 sleep）

### Requirement: frame_timer auto-resets on large discontinuity
当 `frame_timer_ - Clock::Now() < -kMaxSleepSeconds` 时，系统 SHALL 重置 `frame_timer_ = Clock::Now()`，并立即显示当前帧（不丢帧）。

#### Scenario: Seek causes frame_timer reset
- **WHEN** Seek 导致 frame_timer_ 比 Clock::Now() 落后 700ms
- **THEN** frame_timer_ 重置为 Clock::Now()，当前帧立即显示

#### Scenario: Pause-resume causes frame_timer reset
- **WHEN** 暂停 5 秒后恢复，frame_timer_ 远落后于 Clock::Now()
- **THEN** frame_timer_ 重置为 Clock::Now()，恢复后第一帧立即显示

### Requirement: Adaptive sync_threshold based on frame interval
同步阈值 SHALL 为 `clamp(delay, kSyncThresholdMin, kSyncThresholdMax)`，确保：
- 下限 kSyncThresholdMin（40ms）：高帧率时不低于人耳感知阈值
- 上限 kSyncThresholdMax（100ms）：低帧率时不超过唇音同步感知边界

#### Scenario: Low framerate uses larger threshold
- **WHEN** 10fps 视频（delay=100ms），video_pts 比主时钟超前 50ms
- **THEN** sync_threshold = 100ms，50ms < 100ms，不触发修正

#### Scenario: High framerate uses minimum threshold
- **WHEN** 60fps 视频（delay≈16ms）
- **THEN** sync_threshold = clamp(16ms, 40ms, 100ms) = 40ms

#### Scenario: Normal framerate uses delay as threshold
- **WHEN** 25fps 视频（delay=40ms）
- **THEN** sync_threshold = clamp(40ms, 40ms, 100ms) = 40ms

#### Scenario: Low framerate clamped to upper limit
- **WHEN** 5fps 视频（delay=200ms）
- **THEN** sync_threshold = clamp(200ms, 40ms, 100ms) = 100ms（不再是 200ms）

### Requirement: Sleep error auto-compensated across frames
由于 actual_wait 基于绝对时间（frame_timer_ - now）而非相对 delay，前一帧 sleep 的系统调度误差 SHALL 被后续帧自动补偿。

#### Scenario: Over-sleep compensated next frame
- **WHEN** frame_timer_=1040, sleep(40ms) 实际耗了 42ms（now=1042）
- **THEN** 下一帧 frame_timer_=1080, actual_wait = 1080-1042 = 38ms（自动少等 2ms）
