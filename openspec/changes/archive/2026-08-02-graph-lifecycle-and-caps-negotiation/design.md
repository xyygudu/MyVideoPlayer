## Context

现有生命周期为 `Negotiate → Prepare`，约定 Negotiate 不分配资源。但 DemuxNode 必须打开文件才能读到 `codecpar` 并发布输出格式，于是在 `Negotiate()` 里调用 `avformat_open_input` + `avformat_find_stream_info`，破坏了该约定。GStreamer 的阶段顺序恰好相反：`NULL→READY` 先打开设备（原文：*"Device sinks and sources typically try to probe the device to constrain their caps. The element opens the device"*），`READY→PAUSED` 再协商 caps。

同时，协商是单向的（下游读上游 `Format` 自我配置），没有"下游需求上行"的通道。GStreamer 的协商基本规则是 *"downstream suggests formats, upstream decides on format"*，通过 `CAPS`/`ACCEPT_CAPS` query 实现。上一轮的 `needs_global_header` 就是缺失该通道的产物。

约束：本项目的图是**静态连线**的（`Connect()` 早于所有生命周期阶段），端口数量在建图时固定，因此"建图前用 `SourceProbe` 探测以选择流"是静态图模型的必然要求，予以保留；GStreamer 靠动态 pad 免除这一步，不在本次范围。

## Goals / Non-Goals

**Goals:**
- 消除"协商期分配资源"的规则例外，使其对未来所有 source 节点（摄像头、屏幕捕获、网络流）一致成立。
- 建立通用的"下游需求上行"机制，让 global header 及未来同类需求走同一条路径。
- 激活 `FormatCaps`，让端口兼容性校验真正生效。
- 清理死代码与冗余 setter。

**Non-Goals:**
- 时钟选定与分发（`ProvideClock`）——后续独立 change。
- 动态端口 / 运行时重协商（GStreamer 的 `RECONFIGURE`）。
- 消除 SourceProbe 与 DemuxNode 的双重 open —— 静态连线模型下无法消除，需动态端口才可解决。

## Decisions

### D1：新增 `Open` 阶段，而非放宽 Negotiate 的约束
- `INode::Open()` 默认 no-op；`MediaGraph::Open()` 按拓扑序调用，失败时对已 Open 的节点回滚（调用 `Stop()`）。
- **备选（放宽约束）**：承认 source 节点可在协商期打开设备。否决理由：该例外会被每个未来 source 节点引用，例外被反复引用即等于规则失效；且 Negotiate 中途失败时已打开资源的归属不清。
- **备选（注入探测结果）**：把探测得到的 codecpar 传给 DemuxNode，使其 Negotiate 纯化。否决理由：探测工具被迫认识每一种 source 类型，扩展性差；且与本次"减少外部注入"的目标相悖。

### D2：`Open` 的拆除路径复用 `Stop()`
`Stop()` 一并释放 Open 与 Prepare 阶段的资源并回到 `kIdle`；不引入独立的 `Close()`。当前没有"Stop 后重新 Start"的场景，严格对称的收益暂时用不上。

### D3：两趟协商
- Pass 1 `DeclareCaps()`：**逆拓扑序**（Sink→Source），节点在自己的 `InputPort` 上 `SetCaps()` 声明可接受的格式与需求。放在 `Open` 之后，使节点可以基于已打开的设备声明真实能力。
- 图级校验：对每条连接求 `FormatCaps::Intersect`，为空则失败。空 caps 表示"无约束"，不算不兼容。
- Pass 2 `Negotiate()`：**拓扑序**（Source→Sink），节点读 `input_port_->Format()`（上游已定）与 `output_port_->Peer()->Caps()`（下游需求），决定并发布自己的输出格式。
- 校验从 `OutputPort::Connect()` 移出：`Connect` 发生在建图期、早于 `DeclareCaps`，在那里检查协商期才知道的 caps 时序上不成立。

### D4：容器需求编码进 `FormatCaps`
新增 `enum class HeaderPlacement { kAny, kGlobal, kInBand }`，`kAny` 为默认（无约束）。MuxNode 在 `DeclareCaps()` 中按 `AVFMT_GLOBALHEADER` 声明 `kGlobal`/`kInBand`；EncoderNode 在 `Negotiate()` 读下游 caps 得出结论并保存，`Prepare()` 打开编码器时应用。对应 GStreamer 把 `stream-format=avc|byte-stream` 放进 caps 的做法。

### D5：graph 引用由 `AddNode` 自动注入
`AddNode` 时 graph 完全知道自己，无需 facade 补一刀 `SetGraph`。

## Risks / Trade-offs

- **改动面覆盖全部节点与两个 facade** → 分两趟落地并在每步后编译；转码路径用 `mvp_transcode_cli` 自动回归，播放路径由人工验收。
- **播放路径的 sink 改动**（fps/sample_rate 改为从端口读）可能因上游格式未正确传播而回归 → effect 节点已确认透传 `MediaFormat`，且 `VideoFormat::frame_rate` 全链路可用；`Negotiate()` 中增加校验，格式缺失即失败而非静默降级。
- **caps 声明不完善可能误判不兼容** → 采用"空=无约束"语义，节点只声明确有约束的维度。
- EncoderNode 仍保留"协商期发布初步格式、Prepare 后republish 真实 extradata"的两段式（FFmpeg 固有约束），本次不解决，仅在 spec 中显式承认。

## Migration Plan

无部署/回滚需求。facade 需显式改为 `Open() → Negotiate() → Prepare()`；两个 facade 同步修改。
