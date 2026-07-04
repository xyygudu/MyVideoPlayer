## Why

当前 `Link` 使用模板策略（`ByteCapacity` / `CountCapacity`），`OutputPort` 却只创建 `FrameLink<CountCapacity>`，导致：
- **Packet 链路（Demux→Decoder）容量退化为 4 个包**，远小于 FFplay 15MB 的业界标准，4K 视频下 Demux 线程被快速阻塞，音频因数据饥饿而周期性卡顿
- **无法同时约束字节数和条目数**，只能二选一，不能同时满足"不超过 15MB，也不超过一定帧数"的工程需求

## What Changes

- 合并 `ByteCapacity` 和 `CountCapacity` 为一个 `LinkCapacity` 结构体，同时包含 `max_bytes` 和 `max_count`
- `Link` 去模板化，从 `template<typename CapacityPolicy>` 变为普通类，Push 时**任一维度超限则阻塞**
- 各连接使用合适的双维约束：
  - **Packet 链路**（Demux→Decoder）：15MB + 256 条目
  - **视频帧链路**（Decoder→VideoSink）：字节不限 + 3 帧
  - **音频帧链路**（Decoder→AudioSink）：字节不限 + 9 帧
- 移除 `PacketLink` / `FrameLink` 类型别名，统一使用 `Link`
- **BREAKING**: `OutputPort::Connect` 和 `MediaGraph::Connect` 参数从 `int link_capacity` 改为 `LinkCapacity`

## Capabilities

### New Capabilities

- `link-capacity`: Link 双维度容量约束，支持同时按字节数和条目数限制

### Modified Capabilities

<!-- 无 spec 级别行为变更 -->

## Impact

- **5 个文件**：`link.h`、`port.h`、`port.cc`、`media_graph.h`、`media_player.cc`
- 消除模板复杂度，编译期模板实例化减少
- 所有使用 `Connect()` 的地方需更新参数类型
