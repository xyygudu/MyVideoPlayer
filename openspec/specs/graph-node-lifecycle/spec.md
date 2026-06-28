## Purpose

Defines the INode abstract interface, node type categorization, threading modes,
and lifecycle state machine that form the foundation of the MediaGraph node system.

## Requirements

### Requirement: INode defines unified node interface
系统 SHALL 定义 `INode` 抽象接口，所有处理节点（Source/Transform/Sink）必须实现。

INode SHALL 声明以下纯虚方法：
- `bool Negotiate()`：声明/协商端口格式
- `bool Prepare()`：分配资源（codec/surface/device）
- `bool Start()`：启动工作线程（Active）或准备处理（Passive）
- `void Stop()`：停止并等待线程退出
- `void Flush()`：清除内部状态（seek 时调用）
- `std::vector<InputPort*> Inputs()` / `std::vector<OutputPort*> Outputs()`：端口访问
- `NodeType Type()`：Source / Transform / Sink
- `ThreadingMode Threading()`：Active / Passive
- `NodeState State()`：当前状态

Passive 节点在上游线程中被同步调用 Process()。使用回调而非返回值：某些滤镜可能从 1 帧产出 0 帧或多帧。

#### Scenario: Node follows lifecycle sequence
- **WHEN** MediaGraph 调用节点的 Negotiate() → Prepare() → Start()
- **THEN** 节点的 State() 依次为 Idle → Configured → Prepared → Running

### Requirement: NodeType categorizes node roles
系统 SHALL 定义 `enum class NodeType { kSource, kTransform, kSink }`。
- kSource：无输入端口，>=1 个输出端口
- kTransform：>=1 个输入端口，>=1 个输出端口
- kSink：>=1 个输入端口，无输出端口

### Requirement: ThreadingMode selects execution strategy
系统 SHALL 定义 `enum class ThreadingMode { kPassive, kActive }`。
kPassive 节点不拥有独立线程，在上游 Active 节点的 Push 路径中被同步调用。
kActive 节点拥有独立工作线程，通过 Link 连接到相邻 Active 节点。

#### Scenario: Passive transform called on upstream thread
- **WHEN** Active DecoderNode Push 一个帧到输出端口，下游为 Passive AVFilterNode
- **THEN** AVFilterNode::Process() 在 DecoderNode 线程中被调用，不经过 Link

### Requirement: NodeState tracks lifecycle phase
系统 SHALL 定义 `enum class NodeState { kIdle, kConfigured, kPrepared, kRunning, kPaused, kError }`。
状态转换：Idle → Configured → Prepared → Running → Paused / Error。
