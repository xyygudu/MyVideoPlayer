## ADDED Requirements

### Requirement: SyncMode determines synchronization strategy
系统 SHALL 定义 `enum class SyncMode { AudioMaster, VideoMaster }`。Open 时根据流情况确定：有音频流时为 AudioMaster，无音频流时为 VideoMaster。运行期不可切换。

#### Scenario: File with audio uses AudioMaster
- **WHEN** 打开一个包含音频流的文件
- **THEN** sync_mode_ 设为 AudioMaster

#### Scenario: File without audio uses VideoMaster
- **WHEN** 打开一个纯视频文件（无音频流）
- **THEN** sync_mode_ 设为 VideoMaster

### Requirement: VideoMaster mode uses frame-interval timing
在 VideoMaster 模式下，VideoRenderLoop SHALL 基于帧间隔 (`current_pts - last_pts`) 和系统时钟自驱动视频显示节奏。异常帧间隔（<= kFrameDelayMin 或 > kFrameDelayMax）SHALL fallback 到 `1.0 / video_fps_`。

#### Scenario: Normal frame interval timing
- **WHEN** VideoMaster 模式，前一帧 PTS=1.0，当前帧 PTS=1.04
- **THEN** 系统等待约 40ms 后显示当前帧

#### Scenario: Abnormal frame interval uses fallback
- **WHEN** 帧间隔为负值或超过 kFrameDelayMax
- **THEN** 使用 1.0/fps 作为等待时间

### Requirement: VideoMaster mode updates video_clock
在 VideoMaster 模式下，VideoRenderLoop 每渲染一帧后 SHALL 调用 `video_clock_.Set(pts)` 更新视频时钟。

#### Scenario: Video clock tracks rendered frames
- **WHEN** VideoMaster 模式渲染了 PTS=5.0 的帧
- **THEN** video_clock_.Get() 返回约 5.0（加上自 Set 以来的 elapsed）

### Requirement: MasterClock returns active clock reference
PlayerImpl SHALL 提供 `MasterClock()` 方法，AudioMaster 时返回 audio_clock_，VideoMaster 时返回 video_clock_。CurrentPosition() SHALL 统一调用 MasterClock().Get()。

#### Scenario: CurrentPosition reflects master clock
- **WHEN** AudioMaster 模式，audio_clock 为 15.0
- **THEN** CurrentPosition() 返回约 15.0

#### Scenario: CurrentPosition in VideoMaster mode
- **WHEN** VideoMaster 模式，video_clock 为 8.0
- **THEN** CurrentPosition() 返回约 8.0

## MODIFIED Requirements

### Requirement: Video frame display syncs to audio clock
系统 SHALL 在 AudioMaster 模式下，使用 frame_timer 累积校正算法决定视频帧显示时机。具体流程：
1. 计算帧间隔 delay 和音频偏差 diff
2. 用自适应 sync_threshold 判定是否修正 delay
3. 累积 delay 到 frame_timer_
4. 用 frame_timer_ 与 wall-clock 的差值决定 sleep/display/reset

不再直接用 diff 值作为 sleep 时长。丢帧判定改为 frame_timer 重置机制自然覆盖（而非独立的 kSyncThresholdMax 比较）。

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

### Requirement: Audio clock tracks playback position
系统 SHALL 维护一个升级后的 Clock 实例（`audio_clock_`），在 AudioRenderer 每次消费音频帧时调用 `Set(pts)` 更新。Clock 基于 wall-time 外推，Get() 在两次 Set 之间自动推进。

#### Scenario: Audio clock updates during playback
- **WHEN** AudioRenderer 输出了一帧 PCM 数据
- **THEN** `audio_clock_.Set(pts)` 被调用，后续 Get() 返回外推值

#### Scenario: Audio clock advances between frames
- **WHEN** audio_clock_.Set(10.0) 后 20ms 内无新 Set 调用
- **THEN** audio_clock_.Get() 返回约 10.02
