## Context

`MediaFrame` 与 `MediaBuffer` 是整条管线的载体类型。它们之间的元数据分层若含混，代价不是崩溃而是**同一事实的多个副本各自漂移**，且因为副本通常一致，错误可以潜伏很久 —— `media_type()` 恒为 kUnknown 就潜伏至今未被发现。

## Goals / Non-Goals

**Goals**
- 一个 buffer 的时间与类型只有一个真相源。
- `MediaFrame` 只承担"像素/采样数据的 RAII 容器"这一职责。
- 删除已证实零读者的冗余元数据，而非把它们"修正确"。
- 让 `AVFrame::pts` 的有效范围在代码中写明。

**Non-Goals**
- 不改动 `LinkCapacity::ByteSize`（帧按 1 字节计的问题另记于 improvements）。
- 不动 `MakeWritable()` 的双重载（`const&` 版本目前无调用点，但它是一对完整设计的一半，删半边反而破坏语义）。
- 不重命名 `MediaFramePool`（它实际只分配视频帧，命名偏宽，但改名影响面大于收益）。
- 不引入 `MediaBuffer::pts()` 之类的便捷访问器 —— `timestamp().pts` 已清晰，再加一个入口等于又造一个"看起来等价"的路径。

## Decisions

### D1: MediaFrame 退回纯数据

```cpp
class MediaFrame {
    explicit MediaFrame(AVFrame* src);   // 原 (src, pts, type)
    // 保留：width/height/format/PlaneData/PlaneLinesize/MakeWritable/RawFrame/IsValid
    // 删除：pts() / type() / pts_ / type_
};
```

判断依据是"谁需要单独拿到一个 frame 时还需要时间"。逐个核对了所有接收裸 `MediaFrame` 的接口：`VideoRenderer::Render`、`EncoderNode::ConvertVideoFrame`、`pixel_ops`、`VideoSinkNode::current_frame_` —— **无一需要 pts 或 type**。

反向验证：若将来某个 Passive 节点要从 1 帧产出 N 帧（如反交错输出两场），每个输出需要各自的 pts。时间戳在 buffer 上时，节点构造 N 个 buffer 各带各的 timestamp，天然正确；时间戳在 frame 上反而要同时维护两处。新分层对 1→N 场景更友好。

### D2: 删除 MediaBuffer::media_type，而非修正它

`MediaBuffer::media_type()` 全项目零调用点。修正它意味着保留一个"必须正确传递却无人使用"的义务。

更根本的理由：**media type 是链路的属性**。一条 Link 由协商确定格式，其上流过的所有 buffer 类型必然相同，`InputPort::Format().media_type()` 就是权威来源。给每个 buffer 存一份是把已协商的状态复制进数据流。

删除后两个构造函数形状一致：

```cpp
MediaBuffer(AVPacketPtr pkt,  Timestamp ts = {}, BufferFlags flags = BufferFlags::kNone);
MediaBuffer(MediaFrame frame, Timestamp ts = {}, BufferFlags flags = BufferFlags::kNone);
```

`MakeEos(MediaType, int serial)` → `MakeEos(int serial)`：EOS 送往哪一路由 `EmitEos` 选择输出端口决定，类型字段本就不参与路由。

考虑过的替代方案：保留字段并在构造时显式传入 MediaType。否决 —— 那是"把死字段修正确"，既没有消除重复，也保留了后续每个构造点都要传对的负担。

### D3: Timestamp 收窄为 pts + time_base

| 字段 | 写入点 | 读取点 | 处理 |
|---|---|---|---|
| `pts` | 4 | 多处 | 保留 |
| `time_base` | 3 | MuxNode 1 处 | 保留 |
| `dts` | DemuxNode、EncoderNode | **0** | 删除 |
| `duration` | Demux/Decoder/Encoder | **0** | 删除 |

`dts` 并非"不重要"，而是**我们从未定义秒制 dts 的用途**：MuxNode 通过 `av_packet_rescale_ts(pkt, src_tb, dst_tb)` 换算的是 AVPacket 自带的 pts/dts，这条路径不经过 `Timestamp`。真需要时数据就在包里。

`time_base` 仅在包载荷上有意义（帧的 pts 已是秒制，时基只用于把 AVPacket 的原生 pts/dts 换算到目标流）。在帧 buffer 上它是无用数据，但保留字段比按载荷类型分裂结构简单，加注释说明即可。

### D4: AVFrame::pts 的有效范围写进注释

删除 `MediaFrame::pts_` 后，`AVFrame` 内仍有 `pts` 字段，其现状是：

| 位置 | 状态 |
|---|---|
| DecoderNode 入口 | 有效（`av_frame_ref` 拷贝，流时基单位） |
| Effect 输出 | `AV_NOPTS_VALUE`（`MediaFramePool::Acquire` 新建帧） |
| EncoderNode 出口 | 被显式覆写为目标时基 ticks |

即**管线中段 `AVFrame::pts` 是垃圾值**，今天已经如此，只因唯一的读者（编码器）会先覆写而未暴露。这不需要修复，但必须在 `media_frame.h` 写明：它只在进出 FFmpeg 的边界上有效，中段以 `MediaBuffer::timestamp()` 为准。否则将来有人读 `RawFrame()->pts` 会拿到垃圾且无从追查。

### D5: SyncAndRender 接收 MediaBuffer

`VideoSinkNode::SyncAndRender` 既需要 pts 做同步计算，又需要 frame 去呈现。可选：多传一个 `double pts` 参数（变成 4 个参数），或直接传 buffer。

选传 buffer：参数更少，且 buffer 本就是同时携带两者的单元。`PresentFrame(MediaFrame)` 保持不变 —— 上一变更已把时钟推进移出它，因此它天然不需要 pts。

### D6: 不新增 MediaBuffer::pts() 便捷访问器

`buf.timestamp().pts` 会出现约 6 次，加一个 `double pts() const` 能让调用点更短。否决 —— 本变更的主旨正是消除"同一事实的多个入口"，此时再造一个入口（哪怕只是视图）与主旨相悖。`Timestamp` 按值返回 32 字节，每帧一次的开销可忽略。

## Risks / Trade-offs

- **改动面广但类型安全**：约 15~20 处调用点、6 个文件。删除访问器后编译器会全部报错，不存在"漏改且静默"的可能 —— 这是本变更相较前几次的关键优势。
- **行为等价性**：已逐条核对 `mf.pts()` 与 `buf.timestamp().pts` 同源 —— DecoderNode 两者由同一个 `frame_pts` 赋值；效果节点透传 `input.timestamp()` 且帧池 pts 取自 `src_mf.pts()`；ColorEffectNode 的 `MakeWritable()` 保留 pts_。因此替换是等价的。
- **`Timestamp::duration` 删除的潜在影响**：`LinkCapacity` 未来若改为按时长限流（见 improvements），需要重新引入 duration。届时应从 AVPacket/AVFrame 现取或重新加回字段，不构成阻塞。
- **转码路径**：MuxNode 依赖 `ts.time_base`，保留不变；`ts.pts` 用于 `PickNextSlot` 与进度回调，保留不变。转码输出应逐字节一致。

## Migration Plan

单次变更，无兼容期。全部为内部类型。

## Open Questions

无。
