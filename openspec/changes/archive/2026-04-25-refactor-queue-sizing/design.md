## Context

当前 PacketQueue 和 FrameQueue 都使用简单帧数限制。这是初始实现的简化方案，在播放高码率或高分辨率内容时会暴露内存问题。

业界参考实现对比：

| 项目 | PacketQueue 限制 | Video FrameQueue | Audio FrameQueue |
|------|-----------------|------------------|------------------|
| FFplay | 15MB 字节数 | 3 帧 | 9 帧 |
| ijkplayer | 15MB + 时长 | 3 帧 | 9 帧 |
| mpv | 150MB + 时长 | 1–2 帧 | 内联 |
| **当前** | **256 帧** | **16 帧** | **64 帧** |

## Goals / Non-Goals

**Goals:**
- PacketQueue 改为按字节数限制，默认 15MB（对齐 FFplay `MAX_QUEUE_SIZE`）
- Video FrameQueue 默认 3 帧（对齐 FFplay `VIDEO_PICTURE_QUEUE_SIZE`）
- Audio FrameQueue 默认 9 帧（对齐 FFplay `SAMPLE_QUEUE_SIZE`）
- 新增 `ByteSize()` 查询方法，提升可观测性
- 日志增强：Abort/Flush 时输出字节数而非仅帧数

**Non-Goals:**
- 不实现按时长限制（后续再加）
- 不实现动态调整队列大小
- 不修改 FrameQueue 的限制方式（仍按帧数——解码帧大小可预测）

## Decisions

### 1. PacketQueue 改按字节数限制

**理由**：同一个队列里，I 帧（关键帧）几百 KB、B 帧仅几 KB。按帧数限制时：
- 256 个 I 帧 ≈ 可能 50–100MB（4K 高码率场景）
- 256 个 B 帧 ≈ 仅 1–2MB

按字节数（15MB）限制后，无论帧类型分布如何，内存占用都可预测。

**实现**：新增 `total_bytes_` 成员，Push 时 `+= pkt->size`，Pop 时 `-= pkt->size`，Flush 时清零。满队列判定从 `queue_.size() < max_size_` 改为 `total_bytes_ < max_bytes_`。

**备选 — 同时限制帧数+字节数**：增加复杂度，当前不需要。

### 2. Video FrameQueue 降到 3 帧

**理由**：
- 1080p RGB32 解码帧 = 1920 × 1080 × 4 = 8.3MB/帧
- 16 帧 = 133MB；3 帧 = 25MB — 节省 ~108MB
- FFplay 用 3 帧已被证明足够流畅（1帧显示中 + 1帧解码完等待 + 1帧缓冲）
- 帧数过多反而会增大 Seek 时的 Flush 延迟

### 3. Audio FrameQueue 降到 9 帧

**理由**：
- 音频 PCM 帧很小（~4KB/帧），64 → 9 节省内存有限（~220KB），但保持与 FFplay 一致的合理缓冲水平
- 9 帧在 48KHz 采样率下约覆盖 ~0.2 秒音频，足够平滑

### 4. PacketQueue 构造参数改为字节数

将 `PacketQueue(int max_size = 256)` 改为 `PacketQueue(int64_t max_bytes = 15 * 1024 * 1024)`。这是 breaking 内部 API 变更，但 PacketQueue 不是公开接口。

## Risks / Trade-offs

- **[Risk] 极低码率文件缓冲帧数过多** → 15MB 对于低码率（如 500kbps）文件可缓冲 ~240 秒的包，远超需要。后续可叠加时长限制，当前不影响正确性
- **[Risk] Video FrameQueue 3 帧在解码抖动时可能偶尔卡顿** → FFplay 已验证此配置足够；如有问题可微调到 4 帧
- **[Risk] ByteSize 统计的准确性** → 只累计 `pkt->size`（压缩数据大小），不含 AVPacket 结构体开销和对齐填充。与 FFplay 做法一致，误差可忽略
