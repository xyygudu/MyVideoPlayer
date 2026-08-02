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
- `ClockOffer ProvideClock()`：声明本节点可提供的时基与适任度，默认返回空 offer

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

#### Scenario: Node without time base offers nothing
- **WHEN** DecoderNode 等不产生时基的节点被调用 ProvideClock()
- **THEN** 返回空 offer，不参与主时钟仲裁

## ADDED Requirements

### Requirement: ClockOffer carries clock and suitability
系统 SHALL 定义 `struct ClockOffer { std::shared_ptr<IClock> clock; int priority; }`。

`priority` 数值 SHALL 由节点自行声明并定义在节点实现文件内，SHALL NOT 在 `node.h` 中枚举各节点的相对次序——公共头不承载"哪类时基更权威"的语义。数值越大越优先。

#### Scenario: Priority declared by node, not by graph
- **WHEN** 新增一类时钟提供节点
- **THEN** 只需在该节点内声明自身优先级，MediaGraph 的仲裁代码无需改动
