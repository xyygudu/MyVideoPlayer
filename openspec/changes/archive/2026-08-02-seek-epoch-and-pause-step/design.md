## Context

`Link::Flush()` 能清空队列，但清不掉三类"在途"数据：卡在 `Push()` 里手上攥着一份的生产者、正在 Passive 节点 `Process()` 中间的数据、已喂进 `avcodec_send_packet` 尚未 `receive_frame` 出来的帧。世代号（serial/epoch）是解决这个问题的唯一手段 —— ffplay 用 `PacketQueue::serial` + `Frame::serial`，GStreamer 用 event `seqnum`。本项目已有该机制，但只接了一半。

## Goals / Non-Goals

**Goals**
- 陈旧在途**帧**与陈旧在途**包**得到同等隔离。
- 世代的透传与校验成为框架强制的不变量，而非节点的编码纪律。
- 一次 seek = 一个图级世代，不依赖"各条 link 一起加一"的巧合。
- 暂停态能重绘当前帧（窗口 resize 立即生效）。

**Non-Goals**
- 不实现暂停态滤镜参数实时预览（effect 位于 sink 上游，sink 手中的是已处理帧，需要"重跑 effect 链"机制，另议）。
- 不改造 sink 的暂停等待为条件变量（需先设计统一等待抽象，见 improvements）。
- 不修改任何容量参数、不动 `LinkCapacity::ByteSize`。
- 不实现逐帧前进/后退。

## Decisions

### D1: 世代归 MediaGraph，Link 只管排队

`Link` 当前既是有界队列又是世代持有者，两个职责。世代与"某条队列"无关 —— 它描述的是"图整体经历了第几次 seek"。

```cpp
// MediaGraph
std::atomic<int> seek_epoch_{0};
int SeekEpoch() const { return seek_epoch_.load(std::memory_order_acquire); }

void MediaGraph::Seek(double position) {
    seek_epoch_.fetch_add(1, std::memory_order_release);   // 必须先于 Flush
    Flush();
    SendCommand({CommandType::kSeek, position});
    for (auto& c : clocks_) c->Reset(position);
}
```

**递增必须先于 Flush**：`Flush()` 会唤醒阻塞在 `Push` 上的生产者，被唤醒者会立刻把手中的旧数据推入空队列。若世代在其后才递增，存在一个窗口期使这些旧数据看起来"当代"。

`InputPort` 持有 `const std::atomic<int>* epoch_` 而非 `MediaGraph*` —— 最小权限，且避免 `port.h` 反向包含 `media_graph.h`（后者已包含前者，会成环）。绑定发生在 `MediaGraph::Connect`，那里同时看得到图与端口。

### D2: DemuxNode 的本地世代必须"锁存"，不能每包现读

```cpp
// 正确
if (HandlePendingSeek()) { local_serial_ = graph_->SeekEpoch(); }  // 仅 seek 后刷新
...
buf.set_serial(local_serial_);
```

若改成每次 `RoutePacket` 现读 `graph_->SeekEpoch()`，那么"seek 前读出、seek 后推入"的那个包会被盖上**新**世代，正好绕过校验 —— 等于把 bug 固化成设计。锁存点必须紧跟 `av_seek_frame`。

同理，`OutputPort::Push` **不得**自动打 `graph->SeekEpoch()`，只能继承输入 buffer 的世代（见 D4）。

### D3: 校验下沉到 InputPort::Pull

```cpp
std::optional<MediaBuffer> InputPort::Pull() {
    if (!link_) return std::nullopt;
    while (auto buf = link_->Pop()) {
        if (buf->serial() != CurrentEpoch()) {
            SPDLOG_DEBUG("InputPort[{}]: drop stale buffer serial={} epoch={}", ...);
            continue;                       // 预期行为，DEBUG 级
        }
        if (!buf->IsValid()) {
            SPDLOG_WARN("InputPort[{}]: drop invalid buffer", ...);
            continue;                       // 非预期，WARN 级
        }
        return buf;
    }
    return std::nullopt;
}
```

从"每个消费者的义务"变为"端口的不变量"，新节点天然正确。`DecoderNode` 手工的世代检查随之删除；两个 sink 无需新增任何代码即获得保护。

`MediaBuffer::IsValid()` 对 EOS-only buffer 返回 true（现有语义），故 EOS 正常通过。

`DecoderNode::MaybeFlushOnSerialChange` 不受影响：过期数据被 Pull 吃掉后，新数据仍携带新世代，`buf.serial() != last_serial_` 依旧能检测跳变。

### D4: Passive 链路自动继承世代

Passive 节点通过 `OutputPort::Push` 的 emit 回调输出，该回调同时看得到输入与输出：

```cpp
int serial = buf.serial();
downstream->Process(std::move(buf), [downstream, serial](MediaBuffer out) {
    out.set_serial(serial);
    ...
});
```

移除两个 effect 节点的 3 处手工调用。Active 节点（DecoderNode）跨越"包→帧"语义边界，仍需显式表态 —— 但它的输出点只有 `DrainFrames` / `HandleEos` 两处，集中可控。

### D5: MakeEos 强制携带世代

```cpp
static MediaBuffer MakeEos(MediaType type, int serial);
```

若保留无参重载，`DecoderNode::HandleEos` 这类遗漏会导致 EOS 被 Pull 当作过期数据吃掉，**播放永远不报结束**且无任何报错。改成必传参数让编译器兜底。

考虑过的替代方案：在 `Pull()` 中豁免 EOS 校验（`if (HasFlag(kEos)) return buf;`）。否决 —— seek 前排队的旧 EOS 会漏过去，导致 seek 后立刻误报播放结束。

### D6: 暂停态用 step + current_frame，等待仍用轮询

`awating_preview_frame_` 改名并重定义为 `step_`，对齐 ffplay：

```c
// ffplay.c — seek 时若处于暂停，前进一帧后自动重新暂停
if (is->paused) step_to_next_frame(is);
```

即"暂停后 seek 显示目标帧"本质是 **step 一帧**，而非自创的"预览帧"概念。

新增 `MediaFrame current_frame_` 保存最后显示的帧，`RenderFrame` 改为接收右值并移动存入（`AVFramePtr` 移动，无拷贝开销），重绘时直接复用：

```cpp
void VideoSinkNode::RenderFrame(MediaFrame frame) {
    clock_->Set(frame.pts());
    current_frame_ = std::move(frame);
    renderer_->Render(current_frame_);
}
```

代价是常驻一帧引用（4K 约 12MB）—— 这是重绘能力的必要成本，且 `MediaFramePool` 只是延迟回收该缓冲，不额外分配。

等待机制**仍用轮询** `sleep(10ms)`：改条件变量需要 AudioSinkNode 一并改造，而后者的等待条件含"SDL 缓冲水位"这一无事件通知的外部条件，需先设计统一等待抽象。为保持两个 sink 结构对称，本次不动，已记入 improvements。

### D7: kRedraw 复用现有 Command 机制

`graph_command.h` 的注释本就写明"新意图通过扩展枚举加入，不改动 INode 接口"，`kRedraw` 正是其预期用法。

```cpp
void MediaPlayer::Impl::NotifyWindowResized(int w, int h) {
    window_width_ = w; window_height_ = h;
    video_renderer_.Resize(w, h);
    if (graph_) graph_->SendCommand({CommandType::kRedraw});
}
```

MediaPlayer 不持有 sink 指针（`graph-command-control` spec 要求），走 graph 广播正好满足。不关心该意图的节点默认忽略。

## Risks / Trade-offs

- **spdlog 编译期级别**：`SPDLOG_ACTIVE_LEVEL` 此前未定义（默认 INFO），`SPDLOG_DEBUG` 被预处理器整体删除，与 `logging.cc` 运行期设置的 `spdlog::level::debug` 相互矛盾 —— 既有的 `DemuxNode: seek to ...` 调试日志从未输出过。本次在 CMake 中为 Debug / RelWithDebInfo 定义该宏，否则新增的丢弃日志同样不可见、验收项无法完成。
- **EOS 回归**：若 D5 有任何遗漏，症状是"播放到结尾不报结束"，而非崩溃 —— 验收时必须专门测完整播放到结尾。
- **世代绑定遗漏**：若某条连接未绑定 `epoch_`，`CurrentEpoch()` 退化为 0，而生产者标的是真实世代，seek 后该链路数据被**全部**丢弃，表现为画面或声音永久停住。绑定点集中在 `MediaGraph::Connect` 一处，风险可控，但需确认所有连接都经由它建立。
- **常驻帧引用**：VideoSinkNode 持有一帧不释放。4K 约 12MB，可接受；8K 约 99MB，属已知代价（相关容量问题见 improvements）。
- **暂停态 resize 行为变化**：此前不重绘，现在会重绘。若 `ComputeDestRect` 有边界 bug，将从"看不到"变成"看得到"。
- **既有竞态暴露度上升**：`VideoRenderer` 的 `window_width_`/`window_height_` 跨线程非原子读写（先于本次存在），UI 线程写与渲染线程读的时间距离因 `kRedraw` 而缩短。已记入 improvements，本次不修。
- **DEBUG 日志量**：每次 seek 会打印若干条丢弃日志。`SPDLOG_DEBUG` 默认不输出，仅排查时开启。

## Migration Plan

单次变更，无兼容期。`MakeEos` 是内部 API，两个调用点同批修改。

## Open Questions

无。
