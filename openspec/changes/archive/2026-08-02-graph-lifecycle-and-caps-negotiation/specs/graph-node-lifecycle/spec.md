## MODIFIED Requirements

### Requirement: INode defines unified node interface
系统 SHALL 定义 `INode` 抽象接口，所有处理节点（Source/Transform/Sink）必须实现。

INode SHALL 声明以下纯虚方法：
- `bool Negotiate()`：纯格式推理——读上游格式、查下游 caps、决定并发布本节点输出格式。SHALL NOT 分配资源
- `bool Prepare()`：分配资源（codec/surface/device）
- `bool Start()`：启动工作线程（Active）或准备处理（Passive）
- `void Stop()`：停止并等待线程退出，释放 Open 与 Prepare 阶段分配的全部资源
- `void Flush()`：清除内部状态（seek 时调用）
- `std::vector<InputPort*> Inputs()` / `std::vector<OutputPort*> Outputs()`：端口访问
- `NodeType Type()`：Source / Transform / Sink
- `ThreadingMode Threading()`：Active / Passive
- `NodeState State()`：当前状态

INode SHALL 声明以下带默认实现的虚方法：
- `bool Open()`：打开设备/文件等外部资源，默认返回 true（no-op）。Source 类节点在此打开设备，使其在 Negotiate 前即可读到真实格式与能力
- `void DeclareCaps()`：在自身输入端口上声明可接受的格式与需求（`InputPort::SetCaps`），默认 no-op

生命周期顺序 SHALL 为 `Open → DeclareCaps → Negotiate → Prepare → Start`。

Passive 节点在上游线程中被同步调用 Process()。使用回调而非返回值：某些滤镜可能从 1 帧产出 0 帧或多帧。

#### Scenario: Node follows lifecycle sequence
- **WHEN** MediaGraph 调用节点的 Open() → Negotiate() → Prepare() → Start()
- **THEN** 节点的 State() 依次为 Idle → Opened → Prepared → Running

#### Scenario: Source node opens device before negotiation
- **WHEN** DemuxNode::Open() 成功打开文件
- **THEN** 后续 Negotiate() 可直接读取 codecpar 发布输出格式，无需再打开任何资源

#### Scenario: Non-source node ignores Open
- **WHEN** DecoderNode 等无外部设备的节点被调用 Open()
- **THEN** 返回 true 且不分配任何资源

### Requirement: NodeState tracks lifecycle phase
系统 SHALL 定义 `enum class NodeState { kIdle, kConfigured, kOpened, kPrepared, kRunning, kPaused, kError }`。
状态转换：Idle → Opened → Prepared → Running → Paused / Error。
`Stop()` SHALL 释放 Open 与 Prepare 阶段的全部资源并回到 kIdle。

#### Scenario: Open transitions node to kOpened
- **WHEN** 节点 Open() 成功返回
- **THEN** State() 为 kOpened

#### Scenario: Stop releases all resources
- **WHEN** 处于 kRunning 的节点被 Stop()
- **THEN** Open 与 Prepare 阶段分配的资源均被释放，State() 回到 kIdle
