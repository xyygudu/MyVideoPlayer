## Why

当前 AudioMaster 模式的 `ComputeDisplayDelay` 每帧独立计算 sleep 时长（直接 `sleep(diff)`），不维护帧间绝对时间基准。系统调度误差（Windows 典型 ±1.5ms）逐帧累积无法补偿，导致高帧率视频画面轻微抖动（judder）；Seek 后因 frame_timer 不存在，无法自动检测 discontinuity，视频帧被误判为"过期"连续丢弃约 0.5~1s。

## What Changes

- **引入 `frame_timer_` 成员**：绝对时间轴上的虚拟指针，累积每帧理论显示时刻
- **重写 AudioMaster 分支的同步算法**：
  - 计算帧间隔 delay = pts - last_pts
  - 计算音频偏差 diff = pts - audio_clock
  - 自适应阈值 sync_threshold = max(delay, kSyncThreshold)
  - 视频超前时 delay += diff；视频落后时 delay = 0
  - frame_timer_ += delay → actual_wait = frame_timer_ - now
  - actual_wait 过大负值时自动重置 frame_timer_（覆盖 Seek discontinuity）
- **新增常量** `kFrameTimerResetThreshold`：frame_timer 重置判定阈值（-0.1s）
- **移除 AudioMaster 分支对固定 kSyncThreshold 的直接比较**，改为自适应逻辑

## Capabilities

### New Capabilities
- `frame-timer-sync`: frame_timer 累积校正算法——维护绝对时间基准，自动补偿 sleep 误差，支持 discontinuity 自恢复

### Modified Capabilities
- `av-sync`: AudioMaster 模式的视频帧显示时机判定逻辑从"独立 diff 比较"改为"frame_timer 累积 + 自适应阈值"

## Impact

- **代码**：`src/core/src/player.cc`（ComputeDisplayDelay + VideoRenderLoop）、`src/core/src/sync_constants.h`（新增常量）、`src/core/include/mvp/player.h` 或内部 header（新增 frame_timer_ 成员）
- **行为变化**：AudioMaster 模式下帧显示节奏从"每帧独立决策"变为"累积校正"，正常播放体感更平滑；Seek 后第一帧无条件立即显示（不再连续丢帧）
- **兼容性**：VideoMaster 模式不受影响；对外 API 无变化
