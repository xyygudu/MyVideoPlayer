## Why

Seek 2K 60fps H.264 视频时延迟 0.3~0.5s，VS 诊断工具 CPU 采样显示 78% 时间在 `avcodec-61.dll` 软解码。瓶颈是 GOP 内参考帧的逐帧软解——seek 后必须从关键帧解码到目标帧，中间所有帧（包括不被引用的 B 帧）都被完整解码并入队后才丢弃。

## What Changes

- Seek 期间设置 `codec_ctx->skip_frame = AVDISCARD_NONREF`，让 FFmpeg 跳过非参考帧（B 帧）解码，减少约 50-70% 解码量
- Decoder 新增 `SetDropUntilPts(double pts)` 接口，解码出的帧如果 pts < 目标则不入 FrameQueue，避免队列满时阻塞
- Player Seek 流程中调用上述两个机制，到达目标帧后恢复正常解码状态
- Video render loop 中现有的 `seek_target_` 过滤保留作为兜底

## Capabilities

### New Capabilities

（无新增独立模块）

### Modified Capabilities

- `demux-decode`: Decoder 增加 seek 期间的 skip_frame 控制和帧丢弃接口

## Impact

- `src/core/src/decoder.h` / `decoder.cc`: 新增 `SetDropUntilPts()` 接口、`skip_frame` 状态管理
- `src/core/src/player.cc`: Seek 流程中调用 Decoder 的新接口
- `src/core/include/mvp/`: 无公开 API 变化（内部优化）
- 精确度不受影响：B 帧跳过的都是 target 之前会被丢弃的帧，render loop `seek_target_` 兜底保证最终显示帧正确
