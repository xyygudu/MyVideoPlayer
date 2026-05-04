## Why

当前 `sync_threshold = max(delay, kSyncThreshold)` 无上限，极低帧率视频（如 2fps）导致 threshold=500ms，远超人耳感知边界，同步修正失效。同时高帧率视频超前时 `delay += diff` 可能单帧等待过长（如 diff=200ms 时一帧等 216ms），应分散到多帧修正。需对齐 FFplay `compute_target_delay` 的 clamp + FRAMEDUP_THRESHOLD 逻辑。

## What Changes

- **sync_threshold 加上限**：从 `max(delay, kSyncThreshold)` 改为 `clamp(delay, kSyncThreshold, kDropThreshold)`，对齐 FFplay 的 `FFMAX(MIN, FFMIN(MAX, delay))`
- **视频超前分支区分长/短帧间隔**：
  - 帧间隔 > kDropThreshold（低帧率）：`delay += diff`（一次修正，对齐 FFplay `delay > AV_SYNC_FRAMEDUP_THRESHOLD`）
  - 帧间隔 ≤ kDropThreshold（高帧率）：`delay = 2 * delay`（分散修正，避免单帧等待过长）
- **更新 sync_constants.h 注释**：kFrameDelayMin/Max 适用于所有模式，kDropThreshold 重新定义为 sync_threshold 上限 + FRAMEDUP 阈值

## Capabilities

### New Capabilities

（无）

### Modified Capabilities
- `frame-timer-sync`: sync_threshold 计算从 max 改为 clamp；视频超前分支从统一 `delay+=diff` 改为按帧间隔分支处理

## Impact

- **代码**：`src/core/src/player.cc`（ComputeDisplayDelay AudioMaster 分支）、`src/core/src/sync_constants.h`（注释更新）
- **行为变化**：极低帧率视频同步修正恢复有效；高帧率视频超前时修正更平滑
- **兼容性**：25fps 常规视频行为不变（delay=40ms，clamp 后仍为 40ms）
