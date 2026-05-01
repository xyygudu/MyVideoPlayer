## Context

当前 `PlayerImpl::VideoRenderLoop` 已经计算了每帧的 PTS 用于音视频同步，但该值是局部变量，外部无法获取。UI 层通过 100ms QTimer 轮询 `audio_clock_.Get()` 来更新进度条，精度仅为音频 buffer 粒度（~23ms），且无法区分"音频播到哪"和"屏幕显示哪一帧"。

## Goals / Non-Goals

**Goals:**
- UI 能精确显示当前屏幕上显示的视频帧位置和帧号
- 为后续逐帧操作（StepForward/StepBackward）奠定数据基础
- 零额外开销（仅一次 atomic store per frame）

**Non-Goals:**
- 不实现逐帧操作本身（后续 change）
- 不改变音视频同步策略
- 不替换 audio_clock（它仍用于 A/V sync 判定）

## Decisions

### 1. 使用 `atomic<double>` 存储视频 PTS

**选择:** `std::atomic<double> video_pts_` 在 PlayerImpl 中

**备选方案:**
- Signal/Slot 从渲染线程 emit 到 UI → 过于频繁（30~60 次/秒），增加事件队列压力
- shared_mutex + double → 比 atomic 更重，读多写少场景不划算
- 通过 VideoFrameCallback 传递 pts → 回调在渲染线程，UI 仍需跨线程获取

**理由:** atomic<double> 在 x86 上是 lock-free，读写各约 1ns，零竞争。

### 2. UI 层继续使用 QTimer 轮询

**选择:** 保留 100ms QTimer，数据源从 `audio_clock_` 补充为 `video_pts_`

**理由:** VLC 的 Qt 界面也采用此模式（150ms timer）。Timer 做 UI 刷新节拍器，真正的精度由数据源（atomic video_pts_）保证。避免事件驱动的复杂性。

### 3. 进度条读取视频位置，时间标签保留音频位置

**选择:**
- 进度条 slider → 用 `CurrentVideoPosition()` 驱动（帧对齐）
- 时间标签 → 保留 `CurrentPosition()` (audio_clock)，音频是人感知时间的基准

**理由:** 进度条需要和画面一致，时间感知以音频为准（人对音频时间更敏感）。

### 4. FPS 通过 `avg_frame_rate` 获取

**选择:** 从 `AVStream::avg_frame_rate` 读取并缓存

**备选:** `r_frame_rate`（实际帧率）→ 对 VFR 内容更精确但不稳定

**理由:** `avg_frame_rate` 对 CFR 内容精确，对 VFR 内容提供合理近似。帧号显示作为参考信息，不需要 100% 精确。

## Risks / Trade-offs

- [std::atomic<double> 平台兼容性] → C++20 保证 lock-free，MSVC 14.44+ 支持。我们的目标平台无问题。
- [VFR 视频帧号不精确] → 显示帧号仅作参考，不影响功能正确性。
- [audio_clock 与 video_pts 可能有 1~2 帧差异] → 这是正常的 A/V sync 行为，不是 bug。
