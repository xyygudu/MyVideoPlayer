## ADDED Requirements

### Requirement: MessageBus 承载节点上报
`MediaGraph` SHALL 拥有一个 `MessageBus`，通过 `Bus()` 暴露。节点 SHALL 只经 `Bus().Post(GraphMessage)` 向应用层上报，`Post` SHALL NOT 阻塞在消费者上，总线关闭后投递被静默丢弃。

`GraphMessage` SHALL 包含：`type`（`kEos`、`kError`、`kDeviceLost`、`kFirstFrame`）、`source`（投递节点）、`serial`（触发该消息的数据所属 seek 世代）、`detail`（错误描述）、`pts`（`kFirstFrame` 时为该帧显示时间）。

facade SHALL 以单一事件线程 `Wait()` 消费总线，并以 `serial` 与当前世代比较丢弃过期消息；`Shutdown()` SHALL 唤醒消费者并使其退出。

#### Scenario: 投递不受消费者阻塞
- **WHEN** facade 正持锁停止图，而某节点线程在退出前投递 kEos
- **THEN** `Post` 立即返回，节点线程得以结束，`Stop()` 不会死锁

#### Scenario: 过期世代的消息被丢弃
- **WHEN** 视频 sink 投递 serial=3 的 kEos 之后用户发起 seek（世代变为 4）
- **THEN** 事件线程比较 serial 后忽略该 EOS，播放不会被判定结束

#### Scenario: 关闭唤醒消费者
- **WHEN** 事件线程阻塞在 `Wait()` 时调用 `Shutdown()`
- **THEN** `Wait()` 返回空，事件线程退出

## MODIFIED Requirements

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
- `Bus()`：节点向应用层上报的消息总线（见「MessageBus 承载节点上报」）

MediaGraph SHALL 使用 Kahn 算法进行拓扑排序，检测环路。

MediaGraph SHALL NOT 持有播放状态，也 SHALL NOT 在节点线程上回调应用层：播放/转码状态归各 facade 所有，由 facade 的事件线程根据总线消息推进。

#### Scenario: Graph builds topology from connections
- **WHEN** 添加 Source→Transform→Sink 并 Connect
- **THEN** TopologicalSort 产生正确顺序 [Source, Transform, Sink]

#### Scenario: 连接必须声明缓冲量
- **WHEN** 调用 Connect 时未提供容量
- **THEN** 编译失败，不存在"忘记传参即无限缓冲"的路径

#### Scenario: 节点上报不回调应用层
- **WHEN** 解码节点在自身线程遇到不可恢复错误
- **THEN** 它投递 kError 到 `Bus()` 后返回，应用层状态由 facade 的事件线程更新

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

#### Scenario: EOS from all sinks reaches the facade
- **WHEN** 所有 SinkNode 均在当前世代投递 kEos
- **THEN** facade 的事件线程收齐后将自身状态转为结束（播放为 kFinished，转码为 kSucceeded），图本身不持有该状态

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
- **THEN** 该节点无需外部调用 SetGraph 即可通过 graph 的消息总线上报 EOS/错误

### Requirement: MediaFormat extends with codec_params field
编码格式 SHALL 携带 `std::shared_ptr<AVCodecParameters> codec_params`（位于 `EncodedFormat` 分支）。`MediaFormat::FromStream` 工厂 SHALL 深拷贝流的 codecpar，由 shared_ptr 的删除器释放。

#### Scenario: 解码器从端口格式取得 codecpar
- **WHEN** DecoderNode 在 Negotiate 中读取输入端口格式
- **THEN** `AsEncoded().codec_params` 非空，且独立于 demuxer 的 AVStream 生命周期

### Requirement: NodeState kConfigured semantics adjustment
节点的 Prepare() SHALL 同时接受 kIdle 和 kConfigured 两种入口状态；已处于 kPrepared/kRunning 时 SHALL 直接返回成功。

#### Scenario: 未经配置也可准备
- **WHEN** 节点处于 kIdle 时调用 Prepare()
- **THEN** 正常分配资源并进入 kPrepared

### Requirement: MediaFormat 用 variant 拆分音视频
MediaFormat SHALL 用 `std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat>` 承载类型专属字段。公共字段 media_type 和 time_base 留在外层。codec_params SHALL 只在 EncodedFormat 分支。访问通过 IsVideo/IsAudio/IsEncoded + AsVideo/AsAudio/AsEncoded。

#### Scenario: 先判断再访问
- **WHEN** 视频 sink 协商时读取输入格式
- **THEN** 先以 `IsVideo()` 确认分支，再用 `AsVideo()` 读取宽高与帧率

### Requirement: FormatCaps Intersect 泛型化
FormatCaps::Intersect SHALL 用泛型辅助 IntersectVectors<T> 求各维度交集（空列表表示不限制），消除重复的 find-push 逻辑，函数体收缩到 50 行以内。

#### Scenario: 像素格式求交
- **WHEN** 上游支持 {NV12, YUV420P}，下游支持 {YUV420P, D3D11}
- **THEN** 交集为 {YUV420P}

### Requirement: INode 接口控制方法
INode SHALL 提供 `virtual void OnCommand(const Command& cmd)` 和 `virtual void SetPaused(bool)` 的默认空实现。MediaGraph SHALL 提供 Seek/SetPaused/SendCommand 高层操作，按图的规则把命令分发到各节点。

#### Scenario: 不关心命令的节点无需实现
- **WHEN** MediaGraph 广播 kSeek 命令
- **THEN** 未覆写 OnCommand 的节点忽略该命令，关心的节点各自重置状态

### Requirement: MediaGraph 缓存源时长元数据
MediaPlayer SHALL 缓存源 duration（来自 SourceProbe），Duration() 查询读缓存值，不在运行期访问 DemuxNode。

#### Scenario: 播放中查询时长
- **WHEN** 播放过程中调用 Duration()
- **THEN** 直接返回 Open 时探测到的时长，不与 demux 线程交互
