# Link 与 MediaBuffer 数据传输层待改进点

> 记录时间：2026-08-02
> 对标参考：FFplay (FFmpeg)
> 关联讨论：change graph-clock-distribution 归档后的架构复盘

---

## 1. LinkCapacity 的字节维度对帧撒谎，兜底阀被焊死
> **已解决（2026-08-02，change link-capacity-must-be-explicit）**：`ByteSize()` 现在遍历 `AVFrame::buf[]` 与 `extended_buf[]` 累加真实字节；`LinkCapacity` 构造函数私有化，只能经 `ForPackets()` / `ForFrames(depth)` 创建，"无界"不再可表达；容量常量具名于 link.h 并标注 ffplay 出处。下文保留作为问题记录。
### 问题

`LinkCapacity::ByteSize()` 对 MediaFrame 载荷固定返回 1：

```cpp
// Frames: count as 1 byte each (byte limit is typically disabled
// via INT64_MAX for frame links, so this value is irrelevant).
return 1;
```

注释把"我没算"合理化成"反正调用方会禁用字节限制"，即把不变量寄托在调用方配置正确上。`LinkCapacity` 对外宣称是双维度限流器，但其中一个维度对一种载荷类型静默失效。

同时 `media_player.cc` 里 `{15 * 1024 * 1024, 256}` / `{INT64_MAX, 3}` / `{INT64_MAX, 9}` 都是字面量，出处（ffplay 常量）无从查证。

### 影响场景

- **4K 播放**：视频帧链路 3 帧 ≈ 37MB 常驻，无任何限制生效
- **8K / 10bit HDR**：单帧约 99MB，3 帧 ≈ 300MB，仍不触发任何限制，直接吃满内存
- **误配置**：若将来有人给帧链路设了字节上限（比如想限内存），会得到"3 字节 = 3 帧"的荒谬语义，行为完全不符合预期且难以排查
- **诊断缺失**：无法回答"当前管线占了多少内存"

### 改进建议（参考 FFplay 常量出处 + AVFrame buf 引用）

**先厘清两个维度的目标不同，不是同一控制目标的两种度量：**

| 维度 | 真实目标 | 语义单位 |
|---|---|---|
| 帧数 | 吸收解码抖动（I/B 帧耗时差异），给 sink 留前瞻余地 | 帧，与分辨率无关 |
| 字节数 | 防止极端分辨率下内存爆炸 | 字节 |

因此**帧数保持主控**（改成字节主控会导致 240p 下队列深达数百帧、seek 与调参延迟不可接受，8K 下浅到 1 帧而抖动），**字节数作为只在极端分辨率触发的兜底安全阀**。

字节数直接统计 AVFrame 自身持有的缓冲区，无需按 format 分支：

```cpp
static int64_t ByteSize(const MediaBuffer& buf) {
    if (buf.IsPacket()) return buf.AsPacket() ? buf.AsPacket()->size : 0;
    if (buf.IsFrame()) {
        int64_t total = 0;
        const AVFrame* f = buf.AsFrame().RawFrame();
        for (int i = 0; f && i < AV_NUM_DATA_POINTERS && f->buf[i]; ++i)
            total += f->buf[i]->size;
        return total;
    }
    return 0;  // EOS-only
}
```

该写法对视频/音频/planar/packed 统一，且**硬件帧（D3D11）天然计为约 0** —— 它们不占系统内存，正是想要的语义。

容量改为具名常量并注明出处：

```cpp
// 出处：ffplay VIDEO_PICTURE_QUEUE_SIZE / SAMPLE_QUEUE_SIZE / MAX_QUEUE_SIZE
constexpr int     kVideoFrameQueueDepth = 3;
constexpr int     kAudioFrameQueueDepth = 9;
constexpr int64_t kPacketQueueBytes     = 15 * 1024 * 1024;
constexpr int64_t kFrameQueueByteCap    = 128 * 1024 * 1024;  // 仅极端分辨率兜底
```

128MB 兜底下，4K 3 帧（37MB）不触发；8K 10bit 会降到 1 帧 —— 会抖动但不 OOM，优雅降级优于无界增长。

---

## 2. 包队列第二维用固定包数，且字节上限是每链路而非全局

> **已撤销（2026-08-02，change link-capacity-must-be-explicit design D4）**：复核后判定为过度设计。时长维度要求 Link 理解帧率/采样率，把媒体语义滲进一个纯传输设施；15MB 每链路与全局的差别在本项目规模下无实际影响。待出现真实痛点（如直播低延迟场景）再重新评估。

### 问题

当前包链路容量 `{15MB, 256}`。与 ffplay 对照存在两处偏差：

**偏差一 —— 15MB 的作用域。** ffplay 的 `MAX_QUEUE_SIZE` 是**所有队列之和**：

```c
if (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE || ...)
```

我们是每条 link 各 15MB，音视频合计实际上限 30MB。

**偏差二 —— 第二维的量纲。** ffplay 用的是**时长**而非包数：

```c
// stream_has_enough_packets
return queue->nb_packets > MIN_FRAMES /*25*/ &&
       (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
```

即"至少 25 个包**且**够放 1 秒"。我们的固定 256 包对 60fps 视频约 4.3 秒、对 AAC 约 6 秒，缓冲远深于 ffplay。

### 影响场景

- **seek 响应**：缓冲越深，seek 时要丢弃的数据越多，且 demux 线程需要更久才能从 `Push` 阻塞中脱身去执行 `av_seek_frame`
- **低码率长 GOP 流**：256 个包可能只有 1 秒，缓冲不足；高码率短 GOP 流则可能远超 15MB 先被字节维度截断 —— 同一组数字在不同码率下行为差异很大
- **内存**：包数维度无法预测内存占用，实际由 15MB 兜底，等于第二维形同虚设

### 改进建议（参考 FFplay `stream_has_enough_packets`）

将包链路第二维从"包数"改为"时长"，Link 累加 `MediaBuffer::timestamp().duration`：

```cpp
struct LinkCapacity {
    int64_t max_bytes;
    double  max_duration;   // 秒；替代 max_count 用于包链路
    int     max_count;      // 保留，用于帧链路
};
```

时长是与码率、帧率、分辨率都无关的稳定量纲，能让同一组常量在各种流上表现一致。

---

## 3. Link::Push 阻塞生产者，导致同一 demux 的多条支路互相饿死

> **已撤销（2026-08-02，change link-capacity-must-be-explicit design D5）**：证据不足。libavformat 内部已对非交错存储的容器做了重排，实测（非交错 AVI）未观察到支路饿死。在拿到可复现的饿死用例之前不引入生产者自我节流。

### 问题

`Link::Push` 在队列满时阻塞调用线程。DemuxNode 只有一个线程同时向视频链路和音频链路推包，因此**任一条链路满都会卡住整个 demux 线程**，另一条链路即使空着也拿不到数据。

ffplay 的模型相反：`packet_queue_put` **从不阻塞**（队列无界），由 `read_thread` 自己在读之前检查总量，超限则 `sleep`：

```c
if (is->audioq.size + is->videoq.size > MAX_QUEUE_SIZE || (enough_packets...)) {
    SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
    continue;
}
```

即 **ffplay 是生产者自我节流，我们是队列反压生产者**。

### 影响场景

- **解码速度不对称**：视频解码慢于音频时，视频链路先满 → demux 阻塞 → 音频链路饿死 → 音频卡顿。交织良好的容器里不明显，但交织质量差的文件（如某些录制流）会暴露
- **单流分支**：将来若加字幕轨或第二音轨，饿死风险随分支数上升
- **seek 时序**：demux 被阻塞在 `Push` 上时，`Flush()` 唤醒它后它会先把手里那个 seek 前的旧包推进空队列，才执行 `av_seek_frame`
- **seek 时的无效工作**：`MediaGraph::Seek()` 的顺序是 `epoch++ → Flush → SendCommand`。`Flush` 清空队列后、seek 命令送达 demux 之前，demux 线程发现队列空了就全速推包，实测单次 seek 会产生 1~20 个注定被丢弃的旧世代包（含已解码的帧）。调换顺序无法解决 —— 先 SendCommand 再 Flush 会把 seek 后的新包冲掉。只有让 demux 在推包前自行检查 pending seek（即下述改造）才能消除

### 改进建议（参考 FFplay `read_thread` 的聚合水位判断）

在 DemuxNode 侧引入聚合水位检查，读包前先判断，而非让 Push 阻塞：

```cpp
// DemuxLoop
if (TotalBufferedBytes() > kPacketQueueBytes || AllBranchesHaveEnough()) {
    std::this_thread::sleep_for(10ms);
    continue;   // 不读包，也就不会阻塞在任何一条 link 上
}
```

需要 Link 暴露 `BufferedBytes()` / `BufferedDuration()` 供上游查询。这样单条链路满不再影响其他分支，且 demux 线程始终可响应 seek 请求。

---

## 4. MediaFrame 与 MediaBuffer 的元数据三重冗余（已产生一个静默 bug）

### 问题

同一份信息存在多处：

| 信息 | 存储位置 |
|---|---|
| PTS | `MediaBuffer::timestamp_.pts`、`MediaFrame::pts_`、`AVFrame::pts`（原始 time_base） |
| MediaType | `MediaBuffer::media_type_`、`MediaFrame::type_` |

且两个来源**都在被实际使用**：MuxNode 读 `buf.timestamp().pts`，而 VideoSink / AudioSink / EncoderNode 读 `mf.pts()`。DecoderNode 构造时要手写两遍；效果节点透传 buffer 的 timestamp，而输出帧的 pts 由 `MediaFramePool::Acquire(w, h, fmt, src.pts())` 单独传 —— 两条独立写入路径，零一致性校验。

**冗余已经导致一个活着的 bug**：

```cpp
// media_buffer.cc
MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)),      // frame 被移走
      media_type_(frame.type()),       // 读取已移动的对象
```

成员初始化按声明顺序，`payload_` 先于 `media_type_`；而 `MediaFrame` 的移动构造显式重置了源对象：

```cpp
// media_frame.cc
other.type_ = MediaType::kUnknown;
```

结果：**管线中每一个装帧的 MediaBuffer，`media_type()` 恒为 `kUnknown`**。目前未爆是因为没有代码读取帧 buffer 的 `media_type()`。

此外 `media_frame.h` 头注释仍写着 "Used between Decoder → FrameQueue → Renderer boundary"，`FrameQueue` 早已被 `Link` 取代。

### 影响场景

- **潜在崩溃/错路由**：任何将来依赖 `buf.media_type()` 分流的代码（如多轨混音、字幕分派）都会拿到 kUnknown 而走错分支，且无任何日志
- **PTS 分叉**：效果节点若忘记同步两条路径中的任一条，画面时间戳与同步时间戳会不一致，表现为音画不同步但日志正常
- **认知成本**：新增节点时不清楚该读哪个 pts，两个都"看起来对"

### 改进建议

**恢复分层：`MediaFrame` 退回纯数据，时间与类型语义统一由 `MediaBuffer` 承载。**

| | 职责 |
|---|---|
| `MediaFrame` | AVFrame 的 RAII 包装 + 像素/采样访问，**不含 pts / type** |
| `MediaBuffer` | 载荷 + 传输元数据（timestamp / media_type / flags / serial） |

这一步同时消灭上述 `media_type()` 恒为 kUnknown 的 bug —— 冗余字段没了，就不存在读取已移动对象的可能。

迁移影响面：`mf.pts()` 的 4 处调用改为 `buf.timestamp().pts`；`MediaFramePool::Acquire` 去掉 pts 参数；`MediaFrame::CreateSameFormat` 同理。同时更新 `media_frame.h` 中提及 FrameQueue 的过时注释。

---

## 5. 消费端对 buffer 的有效性校验在每个节点重复，且静默吞掉异常

### 问题

每个 Active 消费者都要写同一段三层防御：

```cpp
if (HasFlag(buf.flags(), BufferFlags::kEos)) { ...; continue; }
if (!buf.IsFrame()) { continue; }          // 静默吞掉
MediaFrame& mf = buf.AsFrame();
if (!mf.IsValid()) { continue; }           // 静默吞掉
```

后两个 `continue` 无任何日志，违反项目约定「播放过程中的运行时错误必须通过 spdlog 记录并尝试恢复，不得静默忽略」。且这是"约定"而非"机制"——新节点作者需要自行记得写全。

### 影响场景

- **类型不匹配排查困难**：若上游因协商 bug 推了错误载荷类型，表现为"画面不动"而日志全空
- **新节点遗漏**：任何新增 Active 节点若少写一层，就是一个潜在空指针或 bad_variant_access

### 改进建议

将校验收敛到 `InputPort::Pull()` 这一个边界上，让节点只处理"保证有效的业务数据"。该改动与 serial 过期校验下沉是同一处（见 change 的 seek 序号传播讨论），建议合并处理：

```cpp
std::optional<MediaBuffer> InputPort::Pull() {
    while (auto buf = link_->Pop()) {
        if (buf->serial() != link_->serial()) continue;   // 过期，静默丢弃（预期行为）
        if (!buf->IsValid()) {                            // 非预期，记录后丢弃
            SPDLOG_WARN("InputPort[{}]: dropping invalid buffer", Owner()->Name());
            continue;
        }
        return buf;
    }
    return std::nullopt;
}
```

EOS 仍需向上传递，故 `IsValid()` 对 EOS-only buffer 返回 true 的现有语义正好合适。
