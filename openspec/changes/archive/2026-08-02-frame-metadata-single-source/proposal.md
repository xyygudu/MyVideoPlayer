## Why

管线的元数据存在多个真相源，且其中三处已被证实是**零读者的冗余副本**。

**① PTS 存三份。** `MediaBuffer::timestamp_.pts`（秒）、`MediaFrame::pts_`（秒）、`AVFrame::pts`（流时基）。前两份都在被实际读取 —— MuxNode 读 `buf.timestamp().pts`，而 VideoSink / AudioSink / EncoderNode 读 `mf.pts()`。DecoderNode 构造时要手写两遍，效果节点则透传 buffer 的 timestamp 而单独把 `src_mf.pts()` 传给帧池，**两条独立写入路径、零一致性校验**。

**② `MediaFrame::type_` 与 `MediaBuffer::media_type_` 重复，且已经产生了一个静默 bug。**

```cpp
MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)),   // frame 被移走
      media_type_(frame.type()),    // 读已移走的对象
```

成员按声明顺序初始化，`payload_` 先于 `media_type_`；而 `MediaFrame` 的移动构造显式重置 `other.type_ = kUnknown`。结果：**管线中每一个装帧的 MediaBuffer，`media_type()` 恒为 kUnknown**。

**③ 但这个 bug 之所以从未被发现，是因为字段本身是死的。** 全项目搜索 `MediaBuffer::media_type()`：**零个调用点**。同样地，`Timestamp::dts` 由 DemuxNode 与 EncoderNode 写入、**零处读取**（MuxNode 用 `av_packet_rescale_ts` 换算的是 AVPacket 自带的 pts/dts）；`Timestamp::duration` 由三处写入、**零处读取**。

所以正确的处理不是"把参数传对"，而是**删除**：media type 是**链路的属性**而非每个 buffer 的属性 —— 同一条 Link 上流过的 buffer 类型必然相同，而该类型在协商期已写入 `InputPort::Format()`。给每个 buffer 再存一份，是把已协商的状态复制进数据流，与 pts 三份存储是同一类病。

分层上也错位了：`MediaFrame` 的定位应是**纯数据**（AVFrame 的 RAII 包装 + 像素/采样访问），却越界持有了本属传输层的时间与类型。头注释还写着 "Decoder → FrameQueue → Renderer"，而 `FrameQueue` 早已被 `Link` 取代。

## What Changes

- `MediaFrame` 退回纯数据：删除 `pts_` / `type_` 及其访问器，构造函数收窄为 `MediaFrame(AVFrame* src)`。
- `MediaBuffer` 删除 `media_type_` / `media_type()`。两个构造函数因此形状完全一致，只差 payload 类型；`MakeEos(int serial)` 不再需要类型参数。use-after-move 缺陷随字段一并消失。
- `Timestamp` 删除零读者的 `dts` 与 `duration`，只保留 `pts` 与 `time_base`，并注明 `time_base` 仅对包载荷有意义。
- `MediaFramePool::Acquire(w, h, fmt)` 去掉 pts 参数；删除死代码 `MediaFrame::CreateSameFormat`（已被帧池取代，零调用点）。
- `VideoSinkNode::SyncAndRender` 改为接收 `MediaBuffer`（它需要 pts 做同步计算）；其余 `mf.pts()` 调用点改读 `buf.timestamp().pts`。
- 补充说明 `AVFrame::pts` 的边界语义：仅在进出 FFmpeg 的边界上有效，管线中段一律以 `MediaBuffer::timestamp()` 为准。

## Capabilities

### Modified Capabilities

- `media-frame`: MediaFrame 不再持有 pts / type，退回纯数据载体。
- `media-graph-core`: MediaBuffer 移除 media_type；Timestamp 收窄为 pts + time_base。
- `frame-abstraction`: `MediaFramePool::Acquire` 去掉 pts 参数；移除 `CreateSameFormat`。
- `seek-consistency`: `MakeEos` 签名不再含 MediaType。

## Impact

- 代码：`src/media/media_frame.{h,cc}`、`src/media/graph/media_buffer.{h,cc}`、`src/media/nodes/{decoder_node,transform_effect_node,color_effect_node,encoder_node,video_sink_node,audio_sink_node,demux_node}.{h,cc}`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 行为：纯类型重构，零行为变化 —— 删除的三个字段均无读者，`mf.pts()` 与 `buf.timestamp().pts` 在所有路径上同源。
- 验证优势：删除访问器后编译器会把全部调用点逼出来，配合转码逐字节比对即可，无需依赖人工观察。
