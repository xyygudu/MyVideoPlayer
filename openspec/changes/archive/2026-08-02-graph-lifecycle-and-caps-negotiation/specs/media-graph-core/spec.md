## MODIFIED Requirements

### Requirement: MediaGraph orchestrates node lifecycle and topology
系统 SHALL 定义 `MediaGraph` 类，作为图的拓扑管理器。

MediaGraph SHALL 提供：
- `AddNode(unique_ptr<INode>)`：注册节点并获取所有权，SHALL 自动向节点注入自身引用（节点不再需要外部 `SetGraph`）
- `Connect(OutputPort*, InputPort*, link_capacity)`：连接两个端口
- `Open()`：拓扑排序后按序调用各节点的 Open()；任一节点失败时 SHALL 回滚已 Open 的节点
- `Negotiate()`：两趟协商——先逆拓扑序调用各节点 `DeclareCaps()`，再执行图级 caps 兼容性校验，最后按拓扑序调用各节点 `Negotiate()`
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
