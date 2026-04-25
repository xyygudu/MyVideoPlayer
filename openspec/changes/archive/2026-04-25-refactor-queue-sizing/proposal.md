## Why

当前 PacketQueue 按帧数限制（默认 256），FrameQueue 也按帧数限制（视频 16、音频 64）。这与业界做法不符：

- **PacketQueue 按帧数不合理**：I 帧可达数百 KB，B 帧仅几 KB，256 个 I 帧与 256 个 B 帧的内存差距巨大（可相差 100 倍），无法预测实际内存占用。FFplay / ijkplayer 均按**字节数**限制（15MB）。
- **Video FrameQueue 16 帧过大**：1080p RGB32 解码帧约 8MB/帧，16 帧 = 128MB。FFplay 仅用 3 帧，mpv 用 1–2 帧。
- **Audio FrameQueue 64 帧偏大**：FFplay 用 9 帧。PCM 帧虽小，但 64 帧无必要。
- **缺少队列状态可观测性**：没有 `ByteSize()` / `Duration()` 等查询方法，日志中也只能看到帧数。

## What Changes

- PacketQueue 改为**按字节数**限制（默认 15MB，与 FFplay `MAX_QUEUE_SIZE` 一致），Push 时累加 `pkt->size`，Pop/Flush 时扣减
- FrameQueue 调整默认帧数上限：视频 3 帧（`VIDEO_PICTURE_QUEUE_SIZE`），音频 9 帧（`SAMPLE_QUEUE_SIZE`），与 FFplay 对齐
- PacketQueue 新增 `ByteSize()` 查询方法，日志中输出字节数信息
- 更新所有调用方的构造参数

## Capabilities

### New Capabilities

（无——这是内部实现重构，不引入新的对外能力）

### Modified Capabilities

- `demux-decode`：PacketQueue 的满队列判定条件从帧数改为字节数，FrameQueue 帧数上限变更

## Impact

- **代码**：packet_queue.h/.cc、frame_queue.h/.cc（接口微调）、player.cc（构造参数）、audio_output.cc（构造参数）
- **内存**：视频 FrameQueue 从 ~128MB 降至 ~24MB（1080p）；PacketQueue 上限固定 15MB
- **行为**：队列阻塞触发时机改变（PacketQueue 15MB 而非 256 帧），可能影响极端码率下的缓冲策略，但对常见码率无影响
- **API**：PacketQueue 构造参数含义变化（从 max_frames 改为 max_bytes），内部类不影响外部用户
