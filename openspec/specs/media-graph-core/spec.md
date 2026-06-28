## Purpose

Defines the core MediaGraph infrastructure — the unified data carrier
(MediaBuffer), port format negotiation (MediaFormat), bounded async links
between nodes, and the graph orchestrator that manages topology and lifecycle.

## Requirements

### Requirement: MediaBuffer unifies pipeline data carrier
系统 SHALL 定义 `MediaBuffer` 类型，作为图中所有节点间传输的统一数据载体。payload SHALL 为 `std::variant<AVPacketPtr, MediaFrame>`，禁止使用 void* 或 type-erasure。

MediaBuffer SHALL 携带以下元数据：
- `MediaType media_type`：媒体类型（Audio/Video/Subtitle）
- `Timestamp timestamp`：时间戳信息（pts, dts, duration, time_base）
- `BufferFlags flags`：位标记（kEos, kDiscontinuity, kKeyFrame, kCorrupt）
- `int serial`：管线版本标记，用于 seek/flush 后识别过期帧

MediaBuffer SHALL 为 move-only 语义。`IsPacket()` / `IsFrame()` / `AsPacket()` / `AsFrame()` 提供类型安全访问。

#### Scenario: Packet payload flows through Link
- **WHEN** DemuxNode 产生一个 AVPacket 并包装为 MediaBuffer Push 到下游 Link
- **THEN** DecoderNode 从 Link Pop 后可通过 `AsPacket()` 获取 AVPacket，`IsFrame()` 返回 false

#### Scenario: Frame payload flows through Link
- **WHEN** DecoderNode 解码出 MediaFrame 并包装为 MediaBuffer Push 到下游 Link
- **THEN** 下游节点通过 `AsFrame()` 获取 MediaFrame，`IsPacket()` 返回 false

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
- `AddNode(unique_ptr<INode>)`：注册节点并获取所有权
- `Connect(OutputPort*, InputPort*, link_capacity)`：连接两个端口
- `Negotiate()`：拓扑排序后按序调用各节点的 Negotiate()
- `Prepare()` / `Start()` / `Stop()`：级联调用
- `Flush()`：广播到所有节点和 Link
- `SetClock(IClock*)` / `Clock()`：全局时钟注入
- `SetEventCallback(GraphEventCallback)`：事件分发

MediaGraph SHALL 使用 Kahn 算法进行拓扑排序，检测环路。

GraphEvent 类型 SHALL 包含：kEos, kError, kStateChanged, kFormatChanged。
GraphState 类型 SHALL 包含：kIdle, kReady, kPlaying, kPaused, kFinished, kError。

#### Scenario: Graph builds topology from connections
- **WHEN** 添加 Source→Transform→Sink 并 Connect
- **THEN** TopologicalSort 产生正确顺序 [Source, Transform, Sink]

#### Scenario: Cycle detection prevents invalid graph
- **WHEN** 连接形成 A→B→C→A 环路
- **THEN** TopologicalSort 失败，MediaGraph 报告错误

#### Scenario: EOS from all sinks triggers Finished
- **WHEN** 所有 SinkNode 均发出 kEos 事件
- **THEN** GraphState 转为 kFinished，EventCallback 被调用

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
