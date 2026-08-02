## MODIFIED Requirements

### Requirement: MediaGraph orchestrates node lifecycle and topology
系统 SHALL 定义 `MediaGraph` 类，作为图的拓扑管理器。

MediaGraph SHALL 提供：
- `AddNode(unique_ptr<INode>)`：注册节点并获取所有权，SHALL 自动向节点注入自身引用（节点不再需要外部 `SetGraph`）
- `Connect(OutputPort*, InputPort*, link_capacity)`：连接两个端口
- `Open()`：拓扑排序后按序调用各节点的 Open()；任一节点失败时 SHALL 回滚已 Open 的节点
- `Negotiate()`：三步协商——先逆拓扑序调用各节点 `DeclareCaps()`，再执行图级 caps 兼容性校验，随后仲裁主时钟，最后按拓扑序调用各节点 `Negotiate()`
- `Prepare()` / `Start()` / `Stop()`：级联调用
- `Flush()`：广播到所有节点和 Link
- `MasterClock()`：返回仲裁出的主时钟（非拥有指针，无提供者时为 nullptr）
- `SetEventCallback(GraphEventCallback)`：事件分发

MediaGraph SHALL 使用 Kahn 算法进行拓扑排序，检测环路。

GraphEvent 类型 SHALL 包含：kEos, kError, kStateChanged, kFormatChanged。
GraphState 类型 SHALL 包含：kIdle, kReady, kPlaying, kPaused, kFinished, kError。

#### Scenario: Graph builds topology from connections
- **WHEN** 添加 Source→Transform→Sink 并 Connect
- **THEN** TopologicalSort 产生正确顺序 [Source, Transform, Sink]

## ADDED Requirements

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
`Seek(double)` SHALL 在 Flush 与广播 seek 命令之后，将全部时钟重置到目标位置。

时钟的暂停与重置 SHALL 只有这一条路径，facade SHALL NOT 直接持有或操作时钟对象。

#### Scenario: Pause freezes every clock
- **WHEN** 调用 MediaGraph::SetPaused(true)
- **THEN** 所有节点进入暂停且所有时钟冻结，`MasterClock()->Get()` 不再随墙钟推进

#### Scenario: Seek repositions every clock
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** Link 清空、节点收到 {kSeek, 30.0}、所有时钟重置为 30.0
