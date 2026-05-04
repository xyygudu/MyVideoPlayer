## 1. 修改 ComputeDisplayDelay AudioMaster 分支

- [x] 1.1 将 `sync_threshold = std::max(delay, sync::kSyncThreshold)` 改为 `std::clamp(delay, sync::kSyncThreshold, sync::kDropThreshold)`
- [x] 1.2 将视频超前分支从 `delay += diff` 改为：`delay > kDropThreshold ? delay += diff : delay = 2 * delay`

## 2. 更新 sync_constants.h 注释

- [x] 2.1 更新 kDropThreshold 注释：说明其作为 sync_threshold 上限和帧间隔分界点的双重用途
- [x] 2.2 更新 kFrameDelayMin/Max 注释：适用于所有同步模式（不仅 VideoMaster）

## 3. 验证

- [x] 3.1 编译通过，无 warning
- [x] 3.2 播放常规 25fps 视频确认行为不变
