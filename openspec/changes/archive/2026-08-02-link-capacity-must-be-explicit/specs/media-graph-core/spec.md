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
