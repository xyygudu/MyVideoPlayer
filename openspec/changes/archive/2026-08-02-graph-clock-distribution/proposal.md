## Why

"谁是主时钟"这一决策目前在三处以三种互不相干的判据重复表达：`media_player.cc` 用 `has_audio` 设 `SetSyncMode`、`CurrentPosition()` 用 `audio_stream_index_ >= 0` 二选一取钟、`VideoSinkNode` 用 `sync_mode_ == kAudioMaster` 分支。三者没有任何机制保证一致，接入第三种时钟（外部/系统钟）必须同时改三处。

时钟的拥有者也不是使用者：`MediaPlayer::Impl` 持有两个 `Clock` 实例却一次 `Set()` 都不调，真正写时钟的是两个 sink 节点；facade 退化成"时钟仓库"，并通过 `vsink->SetAudioClock(&audio_clock_)` 手工为节点之间穿线——这条依赖边不在图里，与上一轮刚移除的 `needs_global_header` 是同一种病。而 Transcoder 完全不涉及时钟，说明时钟不是 graph 的固有属性，是**某些节点提供的能力**。

`MediaGraph::SetClock/Clock()` 与 `IClock` 定义后零调用，`mvp::Clock` 甚至没有继承 `IClock`。此外 `Clock` 实现的只是 Linux `seqcount_t`（单写者），却被 UI 线程与音频线程并发写入，是一个已存在的数据竞争。

## What Changes

- 新增 `INode::ProvideClock()` 返回 `ClockOffer{clock, priority}`：节点自行声明"我能提供时基"及适任度，`MediaGraph` 按优先级仲裁出 master，无需知道"音频比视频更适合当钟"这类节点语义。
- `MediaGraph::Negotiate()` 插入第三步 `SelectMasterClock()`，与 `DeclareCaps → ValidateCaps → Negotiate` 同构（声明 → 仲裁 → 消费）。新增 `MasterClock()` 查询接口，替换死代码 `SetClock/Clock()`。
- **移除 `SyncMode`**：VideoSinkNode 改判"主时钟是不是我自己"——是则自由走时，否则对齐主时钟。真正的区别是**有无外部参考时基**，而非"音频主/视频主"。
- 时钟归节点所有：`AudioSinkNode`/`VideoSinkNode` 各自持有 `shared_ptr<Clock>`。移除 `SetAudioClock`/`SetVideoClock`/`SetSyncMode` 及 `MediaPlayer::Impl` 的两个 Clock 成员。
- pause/seek 的时钟写入收归 `MediaGraph::SetPaused/Seek` 单一广播路径，消除 facade 与 graph 的双路径。
- `IClock` 从 `graph/media_graph.h` 下沉到 `clock.h`（`mvp::graph` → `mvp`），补 `Reset(double)`；`Clock : public IClock`。
- **修复既有数据竞争**：`Clock` 补齐写者互斥，成为完整的 `seqlock_t`（`seqcount_t` + 写者锁），读端保持无锁。

## Capabilities

### New Capabilities

（无新增 capability）

### Modified Capabilities

- `av-sync`: 移除 `SyncMode`；主时钟由 graph 按 `ClockOffer` 优先级仲裁；VideoSinkNode 按"是否为主钟"选择走时策略。
- `media-graph-core`: `SetClock/Clock()` → `MasterClock()`；`Negotiate()` 增加主时钟仲裁步；`SetPaused/Seek` 广播至所有时钟。
- `graph-node-lifecycle`: INode 新增 `ProvideClock()`；`ClockOffer` 定义。
- `graph-sink-nodes`: 两个 sink 各自持有并提供时钟；VideoSinkNode 消费 `MasterClock()`。
- `wall-clock`: `Clock` 实现 `IClock`；SeqLock 补齐写者互斥以支持多写者。
- `source-probe`: `CurrentPosition()` 改为查询 `MasterClock()`。
- `graph-playback`: 移除"根据有无音频自动设 SyncMode"表述。
- `frame-timer-sync`: 同步算法的参考基准由 `audio_clock` 改述为"主时钟"。
- `graph-command-control`: Seek 的时钟重置由 graph 内部完成，facade 不再单独重置。

## Impact

- 代码：`src/media/clock.{h,cc}`、`src/media/graph/{node.h,media_graph.h,media_graph.cc}`、`src/media/nodes/{audio_sink_node,video_sink_node}.{h,cc}`、`src/media/media_player.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 行为变化：EOS 时由 `graph_->SetPaused(true)` 统一冻结（此前仅冻结时钟、不暂停节点）。
- 风险：改动位于 A/V 同步核心路径，需人工验收播放同步与 seek/pause 行为。
