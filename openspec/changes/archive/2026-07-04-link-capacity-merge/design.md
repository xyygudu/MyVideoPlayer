## Context

当前 `Link` 是模板类 `template<typename CapacityPolicy> class Link`，通过 `ByteCapacity`（按字节）或 `CountCapacity`（按条目）两种策略限制队列容量。但实际使用中 `OutputPort` 只创建 `FrameLink<CountCapacity>`，默认 `max_count=4`，导致：

- **Packet 链路（Demux→Decoder）**：用 CountCapacity 限制条目数而不是字节数，4 个包远小于 FFplay 15MB 的标准。4K 视频下 Demux 线程推 4 个视频包后立即阻塞，无法继续读取音频包，约 300ms 后音频缓冲枯竭 → 周期性卡顿
- **帧链路容量未对齐 FFplay**：VideoFrame 应为 3 帧（`VIDEO_PICTURE_QUEUE_SIZE`），AudioFrame 应为 9 帧（`SAMPLE_QUEUE_SIZE`）

## Goals

- 合并 `ByteCapacity` 和 `CountCapacity` 为单一 `LinkCapacity`，同时支持按字节数和条目数限制
- `Link` 去模板化，消除没有实际用处的模板策略
- Packet 链路恢复 15MB 字节约束，帧链路对齐 FFplay（视频 3 帧，音频 9 帧）

## Decisions

### 1. LinkCapacity 结构体设计

```cpp
struct LinkCapacity {
    int64_t max_bytes{std::numeric_limits<int64_t>::max()};  // 不限字节
    int     max_count{std::numeric_limits<int>::max()};       // 不限条目
};
```

- 默认构造产生"双无限"容量，由调用方按需设限
- sentinel 值 `INT64_MAX` / `INT_MAX` 表示该维度不限制
- 优点是类型明确、无模板开销、可扩展（后续加 `max_duration` 只需加字段）

### 2. 阻塞条件改为"任一维度超限"

```
IsFull = count >= max_count  OR  total_bytes >= max_bytes
```

Push 时两把锁同时生效。例如 Packet 链路设置 `{15MB, 256}`，实际先达到 15MB 则阻塞字节维度；如果全是极小的包，256 条后也会阻塞，避免无限堆积。

### 3. 字节统计规则

字节统计复用 `ByteCapacity::Size()` 的逻辑：

```cpp
static int64_t ByteSize(const MediaBuffer& buf);
```

- 如果是 Packet（`buf.IsPacket()`）：返回 `pkt->size`
- 如果是 Frame：返回 1（帧链路字节维度通常设为不限，计 1 不造成阻塞）

### 4. 具体容量参数

| 连接 | max_bytes | max_count | 依据 |
|------|-----------|-----------|------|
| Demux→VideoDecoder | 15MB | 256 | FFplay MAX_QUEUE_SIZE + 帧数兜底 |
| Demux→AudioDecoder | 15MB | 256 | 同上 |
| VideoDecoder→VideoSink | 不限 | 3 | FFplay VIDEO_PICTURE_QUEUE_SIZE |
| AudioDecoder→AudioSink | 不限 | 9 | FFplay SAMPLE_QUEUE_SIZE |

### 5. API 签名变更

- `OutputPort::Connect(InputPort*, LinkCapacity)` — 不再接受裸 int
- `MediaGraph::Connect(OutputPort*, InputPort*, LinkCapacity)` — 同上
- `OutputPort::link_` 从 `unique_ptr<FrameLink>` 改为 `unique_ptr<Link>`
- 移除 `PacketLink` / `FrameLink` 类型别名

## Risks / Trade-offs

- **[Risk] 字节统计精度**：只累计 `pkt->size`，不含 `AVPacket` 结构体开销和对齐填充。与 FFplay 做法一致，误差可忽略
- **[Risk] 双维度同时接近上限时的行为**：如有 15MB + 200 个包（未达任一上限），Push 正常。阻塞只在至少一个维度超限时触发
- **[Risk] 已有 `Connect()` 调用点需更新**：共 4 处在 `media_player.cc`，每处需显式传入 `LinkCapacity`，无默认参数隐患
