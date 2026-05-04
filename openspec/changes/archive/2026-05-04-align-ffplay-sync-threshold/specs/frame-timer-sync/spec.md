## MODIFIED Requirements

### Requirement: frame_timer accumulates corrected delay each frame
AudioMaster 模式下，每帧显示前 SHALL 计算修正后的 delay 并累积到 frame_timer_：
1. `delay = pts - last_pts`（帧间隔）
2. `diff = pts - audio_clock`（音频偏差）
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
- **WHEN** 60fps 视频（delay≈16ms），video_pts 比 audio_clock 超前 80ms（超过 sync_threshold 40ms）
- **THEN** delay = 2 × 16ms = 32ms，frame_timer_ 增加 32ms（分散修正，约 80/16 ≈ 5 帧追平）

#### Scenario: Low framerate video ahead uses delay+diff
- **WHEN** 5fps 视频（delay=200ms），video_pts 比 audio_clock 超前 150ms（超过 sync_threshold 100ms）
- **THEN** delay = 200ms + 150ms = 350ms，frame_timer_ 增加 350ms（一次修正）

#### Scenario: Video behind sets delay to zero
- **WHEN** video_pts 比 audio_clock 落后 200ms（超过 sync_threshold）
- **THEN** delay = 0，frame_timer_ 不推进

### Requirement: Adaptive sync_threshold based on frame interval
同步阈值 SHALL 为 `clamp(delay, kSyncThresholdMin, kSyncThresholdMax)`，确保：
- 下限 kSyncThresholdMin（40ms）：高帧率时不低于人耳感知阈值
- 上限 kSyncThresholdMax（100ms）：低帧率时不超过唇音同步感知边界

#### Scenario: High framerate uses minimum threshold
- **WHEN** 60fps 视频（delay≈16ms）
- **THEN** sync_threshold = clamp(16ms, 40ms, 100ms) = 40ms

#### Scenario: Normal framerate uses delay as threshold
- **WHEN** 25fps 视频（delay=40ms）
- **THEN** sync_threshold = clamp(40ms, 40ms, 100ms) = 40ms

#### Scenario: Low framerate clamped to upper limit
- **WHEN** 5fps 视频（delay=200ms）
- **THEN** sync_threshold = clamp(200ms, 40ms, 100ms) = 100ms（不再是 200ms）
