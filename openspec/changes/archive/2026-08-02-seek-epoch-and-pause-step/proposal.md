## Why

暂停后 seek 会显示 seek 前的最后一帧，且永远不再更新 —— 确定性复现。根因是 seek 世代（serial）机制只接了一半：`Link::Flush()` 递增世代、`DemuxNode` 给包打标、`DecoderNode` 校验并丢弃，但 **`DecoderNode` 的两个输出出口（`DrainFrames` / `HandleEos`）从不给帧打标**（下游全部拿到默认值 0），且**两个 sink 从不校验**。

暂停时 sink 停止消费，背压把整条链顶死，`vdec` 线程被卡在 `L2.Push` 里、手上攥着一帧 seek 前的数据。`Link::Flush()` 只能清空队列，清不掉这种"在途"数据 —— 于是 flush 后它立刻被推进空队列，被 `awating_preview_frame_` 唤醒的 render 线程一把捞出来渲染，并顺手清掉标志位，真正的目标帧再也没机会显示。

更深层的问题是责任分配：透传（6 处手工 `set_serial`，漏 2 处）与校验（1 处手工，漏 2 处）都是**靠编码纪律维持的约定**，而非框架强制的不变量。同类的有效性校验（EOS/载荷类型/有效性三层 `if`）同样在每个消费者重复，且两处 `continue` 静默吞掉异常，违反项目「运行时错误不得静默忽略」的约定。

此外 seek 世代本质是**图级**概念，却被存在每条 `Link` 里，靠 `MediaGraph::Flush()` 统一 `+1` 维持一致；`DemuxNode::RefreshLocalSerial` 从**某一条** link 偷看并假设各条相等 —— 一旦将来支持分支级 flush，落后分支的数据会被永久判为过期且无任何日志。

## What Changes

- **世代提升为图级**：新增 `MediaGraph::seek_epoch_`，`Seek()` 中先递增再 flush；`Link` 不再持有 `serial_`；`InputPort` 在 `MediaGraph::Connect` 时绑定世代源。`DemuxNode::RefreshLocalSerial` 改为从 graph 读取，不再窥探 link。
- **校验下沉到 `InputPort::Pull()`**：过期世代与无效载荷在端口边界被丢弃并记录日志，消费者只看到有效的业务数据。移除 `DecoderNode` 里的手工世代检查。
- **Passive 链路自动继承世代**：`OutputPort::Push` 在同步调用 Passive 节点时，由 emit 回调自动把输入世代盖到输出上，移除两个 effect 节点的 3 处手工 `set_serial`。
- **`MediaBuffer::MakeEos(MediaType, int serial)`** 强制传入世代 —— 编译期杜绝遗漏。否则校验下沉后，解码器发出的 EOS 会被当作过期数据丢弃，播放永远不报结束。
- **`DecoderNode` 给输出打标**：新增 `current_serial_`，`DrainFrames()` / `HandleEos()` 据此标记输出。
- **暂停语义正名**：`VideoSinkNode::awating_preview_frame_` 换为 `step_`（对齐 ffplay `step_to_next_frame`），并新增 `current_frame_` 保存最后显示的帧。
- **新增 `CommandType::kRedraw`**：窗口 resize 时经 graph 广播，使**暂停态也能重新布局画面**（当前 `VideoRenderer::Resize()` 只记录尺寸，实际布局发生在下一次 `Render()` 中，暂停时无帧流过因而不生效）。

## Capabilities

### New Capabilities

（无新增 capability）

### Modified Capabilities

- `seek-consistency`: 世代由图持有；陈旧在途**帧**（而不只是包）同样被隔离；校验在端口边界统一执行；EOS 必须携带世代。
- `media-graph-core`: 新增 `seek_epoch_` 与 `SeekEpoch()`；`Seek()` 先递增世代再 flush；`Connect()` 为输入端口绑定世代源。
- `link-capacity`: `Link` 移除 `serial_`，只负责有界排队与 flush。
- `graph-sink-nodes`: VideoSinkNode 保存当前帧，支持 step 与 redraw；sink 不再自行校验世代。
- `graph-command-control`: 新增 `kRedraw` 意图。

## Impact

- 代码：`src/media/graph/{link.h,port.h,port.cc,media_graph.h,media_graph.cc,media_buffer.h,media_buffer.cc,graph_command.h}`、`src/media/nodes/{demux_node,decoder_node,video_sink_node,transform_effect_node,color_effect_node}.{h,cc}`、`src/media/media_player.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 行为变化：暂停态 resize 会立即重新布局（此前需恢复播放才生效）。
- 风险：改动位于 seek 与渲染路径，CLI 无法覆盖，需人工验收四种 seek 场景。
- 范围外（已记入 `docs/improvements/`）：sink 暂停等待改条件变量、`VideoRenderer` 窗口尺寸跨线程非原子读写、`Link::Push` 阻塞导致支路互相饿死、`LinkCapacity::ByteSize` 对帧撒谎、`MediaFrame`/`MediaBuffer` 元数据冗余。
