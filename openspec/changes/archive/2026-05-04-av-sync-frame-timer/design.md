## Context

当前 `PlayerImpl::ComputeDisplayDelay` 的 AudioMaster 分支是一个无状态函数——每帧独立计算 `diff = pts - audio_clock`，直接返回 sleep 时长或丢帧指令。调用处 `VideoRenderLoop` 维护 `last_pts` 和 `last_display_time` 局部变量，但这两个值仅用于 VideoMaster 分支。

这种设计在 25fps 等普通场景足够，但存在两个已知问题：
1. sleep 系统误差逐帧累积（无补偿机制）
2. Seek 后 audio_clock 领先 video_pts → 连续丢帧（无 discontinuity 检测）

参考实现：FFplay 的 `video_refresh` 使用 `frame_timer` 绝对时间锚点 + `compute_target_delay` 修正。

## Goals / Non-Goals

**Goals:**
- 在 AudioMaster 模式下引入 frame_timer 累积校正，消除 sleep 误差累积导致的 judder
- 实现自适应 sync_threshold（`max(delay, kSyncThreshold)`），适配不同帧率
- Seek/discontinuity 后 frame_timer 自动重置，第一帧无条件立即显示
- 保持 `ComputeDisplayDelay` 的可测试性（纯计算逻辑，参数传入而非全局状态）

**Non-Goals:**
- 不实现连续丢帧保护（improvement #2，单独 change）
- 不改动 VideoMaster 分支（已正常工作）
- 不改动 Clock 类实现
- 不引入 frame drop 计数统计

## Decisions

### 1. frame_timer_ 作为 PlayerImpl 成员变量

**选择**：`double frame_timer_` 存储在 `PlayerImpl` 中，VideoRenderLoop 启动时初始化为 `Clock::Now()`。

**替代方案**：
- 局部变量放在 VideoRenderLoop 内 → 可行，但 frame_timer 概念上属于播放器同步状态，未来 Seek 时可能需要外部重置
- 独立 SyncController 类 → 过度设计，当前只有一个消费者

**理由**：最小改动原则。frame_timer_ 与 audio_clock_、video_clock_ 同级，语义清晰。

### 2. ComputeDisplayDelay 签名变更为有状态

**选择**：将 `ComputeDisplayDelay` 从 `const` 方法改为非 const，内部直接读写 `frame_timer_`。返回值语义从"sleep 时长"变为"距离目标显示时刻的等待时间"。

**替代方案**：
- 传入/传出 frame_timer 参数（纯函数风格）→ 调用处代码更啰嗦，且 frame_timer 只有这一个使用者
- 在 VideoRenderLoop 中做 frame_timer 逻辑，ComputeDisplayDelay 只返回修正后的 delay → 职责分散

**理由**：frame_timer 的推进和重置是同步逻辑的核心部分，属于 ComputeDisplayDelay 的职责范围。

### 3. 自适应阈值 sync_threshold = max(delay, kSyncThreshold)

**选择**：动态计算阈值，确保低帧率视频（10fps, delay=100ms）的正常帧间偏差不触发修正。

**替代方案**：
- 固定 kSyncThreshold → 10fps 视频会 judder（已在 interview-qa 中分析）
- 用 kSyncThreshold 的倍数（如 2×） → 高帧率时容忍度过大

**理由**：对齐 FFplay `FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay))`，但简化为 `max(delay, kSyncThreshold)` 因为我们已经有 kMaxSleepSeconds 做上限保护。

### 4. frame_timer 重置条件：actual_wait < -kFrameTimerResetThreshold

**选择**：当 `frame_timer_ - now < -0.1s` 时，重置 `frame_timer_ = now`。阈值复用 `kMaxSleepSeconds`（0.1s）。

**替代方案**：
- 新增独立常量 kFrameTimerResetThreshold → 增加配置复杂度，实际值相同
- 不重置，依赖 delay=0 快速追帧 → Seek 后可能需要多帧才能追上

**理由**：-0.1s 意味着 frame_timer 已经远远落后于 wall-clock（通常是 Seek 导致），此时应直接重锚定而非逐帧追赶。复用 kMaxSleepSeconds 减少常量数量。

### 5. VideoRenderLoop 不再维护 last_display_time（AudioMaster 模式下）

**选择**：AudioMaster 分支的时间基准完全由 frame_timer_ 承载，`last_display_time` 局部变量仅保留给 VideoMaster 分支使用。

**理由**：避免两套时间基准并存导致混乱。

## Risks / Trade-offs

- **[风险] frame_timer 首帧初始化时机** → 在 VideoRenderLoop 开头初始化为 `Clock::Now()`。如果 Pop 第一帧耗时较长（队列空等待），frame_timer 会略微滞后 → 第一帧 actual_wait 为负，触发重置，等价于立即显示。可接受。
- **[风险] Pause/Resume 期间 frame_timer 不更新** → Resume 后 `frame_timer_ - now` 为大负值，触发重置。行为正确——Pause 后第一帧立即显示。无需额外处理。
- **[权衡] ComputeDisplayDelay 变为有状态** → 单元测试需要构造完整 PlayerImpl 或 mock frame_timer_。可接受，同步逻辑本身就是 stateful 的。
- **[权衡] 不做连续丢帧保护** → 极端情况下（解码持续跟不上）仍会连续丢帧。这是独立 improvement，本次不解决。
