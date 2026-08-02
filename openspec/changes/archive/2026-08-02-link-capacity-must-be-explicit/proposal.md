## Why

`LinkCapacity` 的默认值就是**无界**：

```cpp
struct LinkCapacity {
    int64_t max_bytes{std::numeric_limits<int64_t>::max()};
    int     max_count{std::numeric_limits<int>::max()};
};
```

而 `Connect()` 的容量参数带默认值，于是 `transcoder.cc` 里这两行**忘记传参就静默关闭了背压**：

```cpp
graph_->Connect(dec->Outputs()[0], enc->Inputs()[0]);          // 无界
graph_->Connect(enc->Outputs()[0], mux->Inputs()[mux_port_index]);  // 无界
```

EncoderNode 是 Active 节点，Link 确实创建了，但 `IsFull()` 永远为假 —— 解码线程可以无限领先编码线程，堆积量没有任何上限。

**实测（4K60 转码，x264 medium）：修复前 3733MB，修复后 3407MB，降低 326MB（约 26 帧）。**

必须说明的是：这 326MB 才是无界队列的实际堆积量，**其余约 3.4GB 位于 libx264 与 libavcodec 内部，不由本缺陷造成**（隔离方法与数据见 design D6）。因此本变更的价值不在于"省下几个 GB"，而在于：

1. **无界按定义就没有上限** —— 326MB 只是"4K 解码 vs x264 medium"这一特定速度比下的表现。换成快速 preset 或降分辨率编码，堆积量完全不同且不可预测。
2. **失败模式是静默的** —— 不报错、不崩溃，只是吃内存，因此可以长期潜伏。
3. **根因与堆积量无关**：一个"忘记传参就静默失去保护"的默认值。安全的默认应当是有界；更进一步，"无界"这个状态对 Active 下游而言永远是错的，根本不该可表达。

与之互补的第二个缺陷：即便配上字节上限也无效，因为 `ByteSize` 对帧固定返回 1：

```cpp
// Frames: count as 1 byte each (byte limit is typically disabled
// via INT64_MAX for frame links, so this value is irrelevant).
return 1;
```

注释把"我没算"合理化成"反正调用方会禁用"，即把不变量寄托在调用方配置正确上。**两处必须一起修 —— 单修任何一处都不产生保护。**

此外容量参数以字面量散落在两个 facade 的 5 处，出处（ffplay 常量）无从查证。

## What Changes

- `LinkCapacity` 构造函数私有化，只能经具名工厂 `ForPackets()` / `ForFrames(depth)` 创建 —— **"无界"成为不可表达的状态**。
- `Link` / `OutputPort::Connect` / `MediaGraph::Connect` 的容量参数**移除默认值**，忘记传参即编译失败。
- `LinkCapacity::ByteSize` 对帧统计真实字节（遍历 `AVFrame::buf[]` 与 `extended_buf[]`），使字节维度成为真正可用的兜底阀。
- 容量常量集中具名并注明出处；转码图两条链路补上容量。

## Capabilities

### Modified Capabilities

- `link-capacity`: 容量必须显式声明且不可为无界；字节维度对帧生效；修正与实现不符的既有场景描述。
- `media-graph-core`: `Connect()` 的容量参数不再有默认值。
- `graph-transcode`: 转码图各连接声明容量。

## Impact

- 代码：`src/media/graph/{link.h,port.h,port.cc,media_graph.h,media_graph.cc}`、`src/media/media_player.cc`、`src/media/transcoder.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 行为变化：转码时解码线程会被背压限速（此前无限制地领先编码器）。吞吐不变 —— 编码器本就是瓶颈。
- 实测收益：4K 转码峰值内存 3733MB → 3407MB（−326MB）；转码输出逐字节一致，耗时无变化。
- 范围外（经复核后撤销的两项，理由见 design D4/D5）：`LinkCapacity` 增加时长维度、DemuxNode 生产者自我节流。
- 范围外（隔离测量中新发现，另记 improvements）：4K 转码的主要内存占用在 libx264 的 `rc_lookahead=40` × `threads=15` 与解码器帧级多线程内部，属编码参数的固有开销。
