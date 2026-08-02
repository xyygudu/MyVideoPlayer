## MODIFIED Requirements

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
