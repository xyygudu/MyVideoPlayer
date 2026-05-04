## Context

当前 ComputeDisplayDelay AudioMaster 分支使用 `sync_threshold = max(delay, kSyncThreshold)`。对比 FFplay `compute_target_delay`：

```c
// FFplay:
sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
// 即 clamp(delay, 0.04, 0.1)

if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
    delay = delay + diff;       // 低帧率（长帧间隔）→ 一次修正
else if (diff >= sync_threshold)
    delay = 2 * delay;          // 高帧率（短帧间隔）→ 分散修正
```

我们遗漏了：(1) threshold 上限，(2) 高帧率超前的 `2*delay` 分支。

## Goals / Non-Goals

**Goals:**
- sync_threshold 用 clamp 限制在 [kSyncThreshold, kDropThreshold] 范围内
- 视频超前分支区分长/短帧间隔，高帧率用 `2*delay` 分散修正
- 更新 sync_constants.h 注释反映实际用途

**Non-Goals:**
- 不引入 FFplay 的 frame_drops_late 统计
- 不实现 peek-next 式丢帧（需 FrameQueue API 扩展，独立改进）
- 不修改 VideoMaster 分支

## Decisions

### 1. 复用 kDropThreshold 作为 sync_threshold 上限和 FRAMEDUP 阈值

**选择**：`kDropThreshold = 0.1` 同时充当 sync_threshold 上限和帧间隔分界点（对齐 FFplay 的 `AV_SYNC_THRESHOLD_MAX == AV_SYNC_FRAMEDUP_THRESHOLD == 0.1`）。

**替代方案**：引入独立常量 `kSyncThresholdMax` 和 `kFrameDupThreshold` → 增加配置复杂度，实际值相同无必要。

**理由**：FFplay 中这两个常量取值相同（都是 0.1s），语义上也一致——"100ms 是人耳感知唇音不同步的边界"。

### 2. 高帧率超前使用 `2*delay` 而非 `delay+diff`

**选择**：当 `delay ≤ kDropThreshold` 且 `diff > sync_threshold` 时，`delay = 2 * delay`。

**理由**：
- `delay+diff` 在 diff 很大时（如 200ms）单帧等 240ms，画面冻结感明显
- `2*delay` 最多等一个帧间隔（如 60fps 等 33ms），多帧逐步追平，体感更平滑
- FFplay 选择此策略的原因：保持帧显示节奏的均匀性

### 3. 更新注释而非重命名常量

**选择**：保持 `kDropThreshold` 名字不变，更新注释说明其双重用途。同时更新 `kFrameDelayMin/Max` 注释为"所有模式"。

**理由**：重命名会导致 git blame 噪音，且名称仍然有意义（它确实是"判定视频落后的阈值"）。

## Risks / Trade-offs

- **[权衡] `2*delay` 修正速度较慢** → 视频超前 200ms 时，60fps 需要约 200/16≈12 帧（约 200ms）才追平。但这恰好是 FFplay 的行为，且比一次冻结 200ms 体感好。
- **[风险] 2fps 极端场景** → delay=500ms，clamp 后 threshold=100ms，正常帧间偏差(如 50ms)仍在阈值内不修正。仅当偏差 > 100ms 时才触发。行为正确。
