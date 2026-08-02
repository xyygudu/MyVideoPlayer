## Why

图的生命周期只有 `Negotiate → Prepare`，与"source 节点必须先打开设备才能知道自己的格式/能力"这一客观依赖冲突，导致 DemuxNode 不得不在 `Negotiate()` 里执行 `avformat_open_input`（资源分配泄漏进协商期）。同时缺少"下游需求向上游回流"的通道，上一轮不得不用 `InputPort::needs_global_header` 这个临时字段传递容器需求；未来的像素格式、H.264 stream-format、buffer pool 等同类需求会不断重复这一 hack。`FormatCaps`、`graph::StreamInfo` 两个抽象定义了却从未接线。

## What Changes

- 新增 `Open` 生命周期阶段：`Open → Negotiate → Prepare → Start`，对齐 GStreamer 的 `NULL→READY→PAUSED`。source 节点在 `Open()` 打开设备/文件，`Negotiate()` 恢复为纯格式推理。`NodeState` 新增 `kOpened`；`Stop()` 仍一并释放全部资源回到 `kIdle`。
- 协商升级为两趟：`DeclareCaps()`（逆拓扑序，下游声明能接受什么/要求什么）→ 图级兼容性校验 → `Negotiate()`（拓扑序，上游决定格式）。对齐 GStreamer "downstream suggests, upstream decides"。
- `FormatCaps` 新增 `HeaderPlacement`（kAny/kGlobal/kInBand）承载容器对全局头的要求；**移除** `InputPort::SetNeedsGlobalHeader/NeedsGlobalHeader` 临时通道。
- `MediaGraph::AddNode` 自动向节点注入 graph 引用，移除 4 处 `SetGraph`。
- Sink 节点职责归位：`VideoSinkNode`/`AudioSinkNode` 在 `Negotiate()` 校验并读取输入格式（fps、sample_rate、channels），移除 `SetVideoFps`，`AudioSinkNode::ReadAudioParams` 从 `Prepare` 移回 `Negotiate`。
- 端口兼容性校验从 `OutputPort::Connect()`（时序上早于协商，恒为空操作）移到图级校验 pass。
- **删除死代码** `graph::StreamInfo`。
- 修正 `port-format-negotiation` 与 `source-probe` 两份 spec 关于 DemuxNode 打开文件时机的互相冲突表述。

## Capabilities

### New Capabilities

（无新增 capability）

### Modified Capabilities

- `graph-node-lifecycle`: INode 新增 `Open()` / `DeclareCaps()`；NodeState 新增 `kOpened`。
- `media-graph-core`: MediaGraph 新增 `Open()`；`Negotiate()` 改为两趟并含兼容性校验；`AddNode` 自动注入 graph 引用。
- `port-format-negotiation`: caps 真正启用（节点声明、图级求交校验）；新增 `HeaderPlacement`；移除 needs_global_header 临时通道；删除 `StreamInfo`。
- `graph-source-nodes`: DemuxNode 在 `Open()` 打开文件，`Negotiate()` 不再分配资源。
- `source-probe`: 更正 DemuxNode 打开文件的阶段表述。
- `graph-sink-nodes`: sink 节点在 `Negotiate()` 读取并校验输入格式。
- `graph-transcode`: MuxNode 通过 `DeclareCaps()` 声明容器的 HeaderPlacement 需求。
- `graph-transform-nodes`: EncoderNode 在 `Negotiate()` 从下游 caps 求交得出 global header 结论。
- `demux-decode`: 移除已过时的 `DemuxNode 实现 ISourceNode::Probe` 需求（`ISourceNode` 与 `graph::StreamInfo` 均已不存在）。

## Impact

- 代码：`src/media/graph/{node.h,media_graph.h,media_graph.cc,port.h,port.cc,media_format.h,media_format.cc}`、`src/media/nodes/*`（全部 8 个节点）、`src/media/media_player.cc`、`src/media/transcoder.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 时钟分发（`ProvideClock`、移除 `SetAudioClock`/`SetVideoClock`/`SetSyncMode`）不在本变更范围，由后续 change 处理。
