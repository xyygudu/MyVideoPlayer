## Context

`Link` 的容量是管线唯一的背压机制。它失效时不会崩溃、不会报错，只会悄悄吃内存 —— 实测 4K 转码 3.73GB 至今无人察觉，正因为它是静默的。

本变更的重点不是"调参"，而是让**失去背压这件事无法发生**。

## Goals / Non-Goals

**Goals**
- "无界容量"成为不可表达的状态，而非需要记得避开的默认值。
- 字节维度对所有载荷类型如实生效，成为真正的兜底阀。
- 容量数字有明确出处，且与它服务的管线放在一起。
- 提供可测量的验收指标（峰值内存），摆脱纯人工观察。

**Non-Goals**
- 不增加时长维度（复核后判定为过度设计，见 D4）。
- 不改造 DemuxNode 为生产者自我节流（复核后判定证据不足，见 D5）。
- 不改变播放侧的缓冲深度（3 / 9 保持 ffplay 取值）。
- 不做 GPU 显存核算 —— 硬件帧不占系统内存，本就不该计入。

## Decisions

### D1: "无界"不可表达，而非"默认无界"

考虑过三种做法：

| 方案 | 问题 |
|---|---|
| 仅在 `transcoder.cc` 补上参数 | 纯补丁。下一个建图的人照样会忘 |
| 把默认值改成保守有界值 | 消除了 OOM，但"这个默认适不适合本链路"仍靠人判断 |
| **移除默认值 + 私有构造 + 具名工厂** | 忘记传参编译失败；无界无法表达 |

采用第三种：

```cpp
class LinkCapacity {
  public:
    static LinkCapacity ForPackets();
    static LinkCapacity ForFrames(int depth);
    int64_t max_bytes() const;
    int max_count() const;
    static int64_t ByteSize(const MediaBuffer& buf);
  private:
    LinkCapacity(int64_t max_bytes, int max_count);
    ...
};
```

私有构造使聚合初始化 `{INT64_MAX, INT_MAX}` 无法写出。对 Active 下游而言无界永远是错的，因此让它不可表达是正确的收窄，而非限制。

`Link` / `OutputPort::Connect` / `MediaGraph::Connect` 三处的容量参数一并去掉默认值。

**已知代价**：连接到 Passive 节点时不会创建 Link，此时容量参数被忽略，但仍需传。判定可接受 —— 节点的 Threading 模式将来可能改变，届时容量已经就位；且"每条连接都要为缓冲量表态"本身是好事。

### D2: 字节统计遍历 AVFrame 的缓冲区引用

```cpp
if (buf.IsFrame()) {
    int64_t total = 0;
    const AVFrame* f = buf.AsFrame().RawFrame();
    if (!f) return 0;
    for (int i = 0; i < AV_NUM_DATA_POINTERS && f->buf[i]; ++i)
        total += f->buf[i]->size;
    for (int i = 0; i < f->nb_extended_buf; ++i)   // >8 平面的 planar 音频
        total += f->extended_buf[i]->size;
    return total;
}
```

不按 format 分支即可覆盖视频/音频/planar/packed。**硬件帧（D3D11）天然计为约 0** —— `buf[0]` 只是一个句柄引用，不占系统内存，这正是想要的语义（GPU 显存由 FFmpeg 的 hw frame pool 自行限额）。

`av_frame_ref` 共享底层缓冲时会重复计数，但管线中每帧只流经一次，且高估是安全方向。

**不会因单帧超限而死锁**：`IsFull()` 判断的是**当前占用**而非放入后的占用，队列为空时任何尺寸的帧都能进，之后才阻塞。超大帧的效果等同于深度退化为 1，是优雅降级。

### D3: 数值与出处

```cpp
// link.h —— 与 Link 同层的约束
inline constexpr int64_t kPacketQueueBytes = 15 * 1024 * 1024;  // ffplay MAX_QUEUE_SIZE
inline constexpr int     kPacketQueueCount = 256;
inline constexpr int64_t kFrameQueueByteCap = 128 * 1024 * 1024;
```

**帧链路的字节上限 128MB**：4K 三帧（37MB）不触发；8K 10bit（约 99MB/帧）会退化到 1 帧 —— 会抖动但不 OOM。字节维度是兜底，帧数才是主控。

**缓冲深度归各 facade**，因为"我这条管线要缓冲多深"是用例决策而非 Link 的属性：

```cpp
// media_player.cc —— 出处：ffplay VIDEO_PICTURE_QUEUE_SIZE / SAMPLE_QUEUE_SIZE
constexpr int kVideoFrameDepth = 3;
constexpr int kAudioFrameDepth = 9;

// transcoder.cc
constexpr int kTranscodeFrameDepth = 4;
```

**转码深度取 4 的依据**：

- 播放侧的 3 来自 ffplay 的显示队列，服务的是 A/V 同步前瞻（`vp_duration` 需要看到下一帧）。转码没有同步需求，这个理由不适用。
- 转码链路的唯一职责是不让编码器饿着。x264 编码单帧（4K，crf23）在百毫秒量级，而 h264 解码单帧在毫秒量级 —— **1 帧缓冲就已足够避免饥饿**。
- 取 4 是为吸收解码抖动（I 帧解码耗时明显高于 P 帧）留余量，代价在 4K 下约 50MB。
- 更大的深度没有收益：**x264 自身已持有 `rc_lookahead=40` 帧的前瞻**（日志可见），编码器内部缓冲远大于链路缓冲，加深链路只是徒增内存。

`enc→mux` 使用 `ForPackets()`：编码后的包很小，且 mux 是纯文件 IO，该链路几乎不会填满，取与 demux 链路一致的口径即可。

### D4: 撤销"增加时长维度"（原 improvements 第 2 条）

原提议为 `LinkCapacity` 增加 `max_duration`，理由是"256 包对不同码率意味着不同的缓冲秒数"。复核后判定为过度设计：

| 场景 | 15MB 折算 | 256 包折算 | 实际生效 |
|---|---|---|---|
| 4K60 @ 50Mbps | 2.5s | 2.5s | 字节先到，**2.5s** |
| 1080p @ 8Mbps | 15.7s | 10.2s | 包数先到，**10.2s** |
| AAC 128kbps | 983s | 5.5s | 包数先到，**5.5s** |

字节与包数两个维度已把缓冲深度夹在 2.5~10 秒，全部高于 ffplay 的 1 秒下限。而三字段结构（每条链路只用其中两个）正是本项目在 Command 结构上批评过的"胖结构体"形态。收益不抵代价。

### D5: 撤销"DemuxNode 生产者自我节流"（原 improvements 第 3 条）

原判断：demux 单线程推两条 link，一条满会卡住另一条造成饿死。复核后证据不足：

1. **libavformat 自身会交织** —— `avidec.c` 对非交织 AVI 有专门处理（选 dts 最小的流并 seek 过去）。测试用的 `big_buck_bunny_480p_surround.avi` 日志中确有 `non-interleaved AVI` 提示，播放完全正常。
2. **缓冲深度 2.5~10 秒**，远大于正常文件的交织间隙。
3. **零实测证据**。

而修复需要引入"软水位 + 硬上限"两级配置，且对病态交织文件 headroom 无法有界 —— 这正是 ffplay 选择无界队列的原因。为假想问题引入两级机制是另一种过度设计。降级为文档说明。

### D6: 实测隔离 —— 本缺陷到底占多少内存

最初的判断是"4K 转码峰值 3733MB 由无界链路造成"。该判断**错误** —— 它是测了一个数就直接归因，未做隔离。实测数据：

| 测量 | 结果 |
|---|---|
| 4K60 修复前（WorkingSet 峰值） | 3733 MB |
| 4K60 修复后 | 3407 MB |
| 差值 | **326 MB ≈ 26 帧** |
| 480p 修复后 | 191 MB (WS) / 225 MB (private) |

关键证据：**4K/480p 内存比 = 17.8×，而像素面积比 = 20.2×**。内存与帧面积成正比、**帧数恒定在约 300**，而本变更后我们的队列最多只缓 4 帧。因此主要占用在第三方库内部：x264 日志显示 `threads=15 lookahead_threads=2 rc_lookahead=40`，光前瞻与帧级并行就需持有几十帧；解码器的帧级多线程与 DPB 又是几十帧。

**结论**：326MB 才是本缺陷的实际堆积量，其余属编码参数的固有开销。本变更仍成立，但理由是"消除一类无上限且静默的失效"，而非"省下几个 GB"—— 326MB 仅是当前速度比下的表现，换一组编码参数即不可预测。

引申的 backlog：若要真正控制 4K 转码内存，该调的是 x264 的 `rc_lookahead` 与 `threads`，不是队列深度。

## Risks / Trade-offs

- **转码吞吐**：解码线程从"无限领先"变为被背压限速。编码器本就是瓶颈，吞吐应不变 —— 需以转码耗时与输出字节数验证。
- **Passive 连接需传无用参数**：见 D1，已判定可接受。
- **8K 以上单帧超过 128MB**：退化为深度 1，可能抖动。属已知边界，优于 OOM。
- **播放侧内存变化**：帧链路的字节维度从"永不触发"变为"128MB 触发"，4K 下仍由帧数主控，行为不变。

## Migration Plan

单次变更，无兼容期。7 处 `Connect` 调用全部改为具名工厂。

## Open Questions

无。
