## Purpose

Defines the core MediaGraph infrastructure — the unified data carrier
(MediaBuffer), port format negotiation (MediaFormat), bounded async links
between nodes, and the graph orchestrator that manages topology and lifecycle.

## Requirements

### Requirement: MediaBuffer unifies pipeline data carrier
系统 SHALL 定义 `MediaBuffer` 类型，作为图中所有节点间传输的统一数据载体。payload SHALL 为 `std::variant<AVPacketPtr, MediaFrame>`，禁止使用 void* 或 type-erasure。

MediaBuffer SHALL 携带以下元数据：
- `Timestamp timestamp`：`pts`（秒）与 `time_base`。`time_base` 仅对包载荷有意义 —— 用于把 AVPacket 原生的 pts/dts 换算到目标流；帧载荷的 pts 已是秒制
- `BufferFlags flags`：位标记（kEos, kDiscontinuity, kKeyFrame, kCorrupt）
- `int serial`：seek 世代标记，用于识别过期在途数据

MediaBuffer SHALL NOT 携带媒体类型：媒体类型是**链路的属性**而非单个 buffer 的属性，同一条 Link 上流过的 buffer 类型必然相同，权威来源是协商期确定的 `InputPort::Format()`。把已协商的状态复制进数据流 SHALL 被避免。

`Timestamp` SHALL NOT 包含无读者的派生字段。秒制的 dts 与 duration SHALL NOT 存在 —— 需要时数据在 AVPacket / AVFrame 内。

两个构造函数 SHALL 形状一致，仅 payload 类型不同。`MakeEos(int serial)` SHALL 不接收媒体类型 —— EOS 送往哪一路由输出端口的选择决定。

MediaBuffer SHALL 为 move-only 语义。`IsPacket()` / `IsFrame()` / `AsPacket()` / `AsFrame()` 提供类型安全访问。

#### Scenario: Packet payload flows through Link
- **WHEN** DemuxNode 产生一个 AVPacket 并包装为 MediaBuffer Push 到下游 Link
- **THEN** DecoderNode 从 Link Pop 后可通过 `AsPacket()` 获取 AVPacket，`IsFrame()` 返回 false

#### Scenario: Frame payload flows through Link
- **WHEN** DecoderNode 解码出 MediaFrame 并包装为 MediaBuffer Push 到下游 Link
- **THEN** 下游节点通过 `AsFrame()` 获取 MediaFrame，`IsPacket()` 返回 false

#### Scenario: 媒体类型来自端口而非 buffer
- **WHEN** 某节点需要知道输入数据是音频还是视频
- **THEN** 它读取 `input_port_->Format().media_type()`，而非从 buffer 上取

#### Scenario: 构造不读取已移动的对象
- **WHEN** 以 MediaFrame 构造 MediaBuffer
- **THEN** 构造函数 SHALL NOT 从被移动的载荷上读取任何元数据

#### Scenario: EOS flag marks end of stream
- **WHEN** Source 节点到达文件末尾
- **THEN** Push 一个 `flags_ & kEos` 为 true 的 MediaBuffer，下游节点据此停止拉取

#### Scenario: Serial detects stale frame after seek
- **WHEN** Seek 后 Flush() 递增 serial 为 5，但队列中仍有 serial=4 的残留 MediaBuffer
- **THEN** 消费端入队时比对 serial，丢弃 serial < 当前值的过期帧

#### Scenario: Move semantics transfer ownership
- **WHEN** MediaBuffer 被 move 到另一个对象
- **THEN** 源对象 payload 变为空 variant，目标对象持有完整数据

### Requirement: MediaFormat describes negotiated port format
系统 SHALL 定义 `MediaFormat` 类，描述端口的协商后格式。对视频包含：width, height, pixel_format, frame_rate；对音频包含：sample_rate, channels, channel_layout, sample_format；对压缩包包含：codec_id。含 `time_base`（AVRational）。

MediaFormat SHALL 不暴露任何 FFmpeg 类型，pixel_format 和 sample_format 使用项目定义的枚举值。

#### Scenario: Video format negotiation
- **WHEN** DecoderNode 输出端口声明格式 {width=1920, height=1080, pixel_format=YUV420P}
- **THEN** 下游 AVFilterNode 输入端口接受该格式，协商成功

#### Scenario: Format mismatch causes negotiation failure
- **WHEN** 上游输出 pixel_format=D3D11，下游仅支持 YUV420P/NV12
- **THEN** MediaGraph::Negotiate() 返回 false，spdlog 报告具体不匹配字段

### Requirement: Link connects nodes with bounded async queue
系统 SHALL 定义 `Link` 模板类，作为两个 Active 节点之间的异步数据通道。Link SHALL 支持两种容量策略：
- `ByteCapacity`：基于字节数（用于压缩包流）
- `CountCapacity`：基于帧数（用于解码帧流）

Link SHALL 提供：
- `Push(MediaBuffer buf)`：阻塞直到容量可用，写入后自动分配 serial
- `Pop()`：阻塞直到数据可用或 link abort，返回 `std::optional<MediaBuffer>`
- `Flush()`：清空队列 + 递增 serial 版本
- `Abort()` / `Reset()`：异常终止与恢复
- 内部同步：`std::mutex` + 条件变量（cond_push / cond_pop）

#### Scenario: Back-pressure blocks producer on full queue
- **WHEN** Link 达到最大帧容量
- **THEN** Push() 阻塞调用线程，直到消费者 Pop 释放容量

#### Scenario: Flush discards stale data and bumps serial
- **WHEN** Seek 导致 Flush()，当前 serial=3
- **THEN** 队列中所有 serial=3 的数据被丢弃，serial 递增为 4

### Requirement: Port represents a typed connection point
系统 SHALL 定义 `InputPort` 和 `OutputPort` 类，作为节点的类型化连接端点。每个 Port 携带：
- `FormatCaps` 能力描述
- `MediaFormat` 协商后格式
- `Link` 底层队列引用（Active 节点绑定）

OutputPort::Connect(InputPort*) SHALL 进行 caps 交集检查。

OutputPort::Push() 的调度规则：
- **Passive 下游**：同步调用下游 Process()，数据不经过 Link
- **Active 下游**：Push 到 Link 队列，由下游线程异步取走

#### Scenario: Passive downstream processes on caller's thread
- **WHEN** Active 节点 Push 到 Passive 节点的输入端口
- **THEN** 不经过 Link，同步调用 Passive 节点的 Process(MediaBuffer, emit)

### Requirement: MediaGraph orchestrates node lifecycle and topology
系统 SHALL 定义 `MediaGraph` 类，作为图的拓扑管理器。

MediaGraph SHALL 提供：
- `AddNode(unique_ptr<INode>)`：注册节点并获取所有权，SHALL 自动向节点注入自身引用（节点不再需要外部 `SetGraph`）
- `Connect(OutputPort*, InputPort*, LinkCapacity)`：连接两个端口。容量参数 SHALL NOT 有默认值 —— 遗漏应当编译失败而非静默退化为无限缓冲
- `Open()`：拓扑排序后按序调用各节点的 Open()；任一节点失败时 SHALL 回滚已 Open 的节点
- `Negotiate()`：三步协商——先逆拓扑序调用各节点 `DeclareCaps()`，再执行图级 caps 兼容性校验，随后仲裁主时钟，最后按拓扑序调用各节点 `Negotiate()`
- `Prepare()` / `Start()` / `Stop()`：级联调用
- `Flush()`：递增 seek 世代并广播到所有节点和 Link
- `MasterClock()`：返回仲裁出的主时钟（非拥有指针，无提供者时为 nullptr）
- `SeekEpoch()`：返回当前 seek 世代，供生产者锁存与消费端校验
- `SetEventCallback(GraphEventCallback)`：事件分发

MediaGraph SHALL 使用 Kahn 算法进行拓扑排序，检测环路。

GraphEvent 类型 SHALL 包含：kEos, kError, kStateChanged, kFormatChanged。
GraphState 类型 SHALL 包含：kIdle, kReady, kPlaying, kPaused, kFinished, kError。

#### Scenario: Graph builds topology from connections
- **WHEN** 添加 Source→Transform→Sink 并 Connect
- **THEN** TopologicalSort 产生正确顺序 [Source, Transform, Sink]

#### Scenario: 连接必须声明缓冲量
- **WHEN** 调用 Connect 时未提供容量
- **THEN** 编译失败，不存在"忘记传参即无限缓冲"的路径

### Requirement: MediaGraph arbitrates the master clock during negotiation
`MediaGraph::Negotiate()` SHALL 在 caps 校验之后、节点 `Negotiate()` 之前收集所有节点的 `ProvideClock()` 结果，选出优先级最高者作为主时钟。该顺序 SHALL 保证节点在自身 `Negotiate()` 中即可读到 `MasterClock()`。

仲裁 SHALL 只在协商期执行一次，运行期不切换。

#### Scenario: Consumer reads master clock during its own negotiation
- **WHEN** VideoSinkNode::Negotiate() 执行
- **THEN** `graph->MasterClock()` 已完成仲裁，返回最终主时钟

#### Scenario: Arbitration is order-independent
- **WHEN** 调整 AddNode 的调用次序但节点集合不变
- **THEN** 仲裁结果不变（由优先级而非添加顺序决定）

### Requirement: MediaGraph broadcasts pause and seek to all clocks
`SetPaused(bool)` SHALL 在暂停/恢复所有节点后，将同一状态广播给图内全部时钟。
`Seek(double)` SHALL 依次执行：`Flush()` → 广播 seek 命令 → 将全部时钟重置到目标位置。seek 世代的递增由 `Flush()` 承担，`Seek()` SHALL NOT 自行递增。

时钟的暂停与重置 SHALL 只有这一条路径，facade SHALL NOT 直接持有或操作时钟对象。

#### Scenario: Pause freezes every clock
- **WHEN** 调用 MediaGraph::SetPaused(true)
- **THEN** 所有节点进入暂停且所有时钟冻结，`MasterClock()->Get()` 不再随墙钟推进

#### Scenario: Seek repositions every clock
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** 世代递增、Link 清空、节点收到 {kSeek, 30.0}、所有时钟重置为 30.0

### Requirement: MediaGraph owns the seek epoch
`MediaGraph` SHALL 持有 `seek_epoch_` 并提供只读访问 `SeekEpoch()`。该计数器 SHALL 是全图唯一的 seek 世代来源。

`MediaGraph::Connect()` SHALL 在建立连接后为下游输入端口绑定世代来源，使端口无需依赖 `MediaGraph` 类型即可读取当前世代。

#### Scenario: 端口以最小权限访问世代
- **WHEN** InputPort 需要判断 buffer 是否过期
- **THEN** 它只持有指向世代计数器的只读引用，不持有 MediaGraph 指针，`port.h` 不包含 `media_graph.h`

#### Scenario: 未连接端口的世代退化安全
- **WHEN** 某输入端口未经 `MediaGraph::Connect` 建立连接
- **THEN** 其 Pull() 直接返回空，不会因缺少世代来源而误判数据

### Requirement: Flush 自持世代不变量
`MediaGraph::Flush()` SHALL 在清空 Link **之前**递增 seek 世代，使「清空队列」与「作废在途数据」成为不可分割的单一操作。

理由：清空队列会唤醒阻塞在 `Push` 上的生产者，它们随即把手中的旧数据入队。若世代递增交由调用方在 `Flush()` 前后自行安排，该不变量就依赖调用方记得配对 —— 单独调用 `Flush()` 将清空队列却不作废在途数据，导致陈旧数据被当作当代数据放行，且无任何报错。

`Flush()` SHALL 因此是独立可安全调用的操作，不要求调用方补做任何配套步骤。

#### Scenario: 单独调用 Flush 亦作废在途数据
- **WHEN** 直接调用 `MediaGraph::Flush()`（不经由 Seek）
- **THEN** 世代递增，此后抵达的 pre-flush 在途数据仍被端口校验丢弃

#### Scenario: 递增先于清空
- **WHEN** Flush 唤醒某个阻塞在 Push 上的生产者，它立即把手中的旧 buffer 入队
- **THEN** 消费者此时读到的已是新世代，该 buffer 被判定为过期

#### Scenario: Cycle detection prevents invalid graph
- **WHEN** 连接形成 A→B→C→A 环路
- **THEN** TopologicalSort 失败，MediaGraph 报告错误

#### Scenario: EOS from all sinks triggers Finished
- **WHEN** 所有 SinkNode 均发出 kEos 事件
- **THEN** GraphState 转为 kFinished，EventCallback 被调用

#### Scenario: Open precedes negotiation
- **WHEN** facade 依次调用 graph->Open() → graph->Negotiate() → graph->Prepare()
- **THEN** 所有节点的 Open() 均在任一节点的 Negotiate() 之前完成

#### Scenario: Open failure rolls back opened nodes
- **WHEN** 某节点 Open() 返回 false
- **THEN** MediaGraph::Open() 返回 false，已成功 Open 的节点被 Stop() 释放资源

#### Scenario: Caps declared in reverse topological order
- **WHEN** MediaGraph::Negotiate() 执行
- **THEN** 先按 Sink→Source 顺序调用 DeclareCaps()，再按 Source→Sink 顺序调用 Negotiate()

#### Scenario: AddNode injects graph reference automatically
- **WHEN** 通过 AddNode 注册一个需要上报事件的节点
- **THEN** 该节点无需外部调用 SetGraph 即可通过 graph 上报 EOS/错误

### Requirement: INode interface removes Configure(NodeConfig)
INode 接口 SHALL 移除 `virtual bool Configure(const NodeConfig& config)` 方法。NodeConfig 结构体 SHALL 被删除。各节点的配置 SHALL 改为构造参数注入或节点专属 setter 方法。

#### Scenario: INode has no Configure method
- **WHEN** MediaGraph 管理节点生命周期
- **THEN** 仅调用 Negotiate/Prepare/Start/Stop/Flush，不调用 Configure

### Requirement: MediaFormat extends with codec_params field
MediaFormat SHALL 新增 `std::shared_ptr<AVCodecParameters> codec_params_` 成员和 `codec_params()` 访问器。FromStream 工厂 SHALL 深拷贝构造。

### Requirement: NodeState kConfigured semantics adjustment
Prepare() SHALL 同时接受 kIdle 和 kConfigured 两种入口状态。

### Requirement: MediaFormat 用 variant 拆分音视频
MediaFormat SHALL 用 `std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat>` 承载类型专属字段。公共字段 media_type 和 time_base 留在外层。codec_params SHALL 只在 EncodedFormat 分支。访问通过 IsVideo/IsAudio/IsEncoded + AsVideo/AsAudio/AsEncoded。

### Requirement: FormatCaps Intersect 泛型化
FormatCaps::Intersect SHALL 用泛型辅助 IntersectVectors<T> 消除重复 find-push 逻辑，函数体收缩到 50 行以内。

### Requirement: INode 接口控制方法
INode SHALL 新增 `virtual void OnCommand(const Command& cmd) {}` 和 `virtual void SetPaused(bool)` 默认空实现。MediaGraph SHALL 新增 Seek/SetPaused/SendCommand 高层操作。

### Requirement: MediaGraph 缓存源时长元数据
MediaPlayer SHALL 缓存源 duration（来自 Probe），Duration() 查询读缓存值，不再运行时访问 DemuxNode。
