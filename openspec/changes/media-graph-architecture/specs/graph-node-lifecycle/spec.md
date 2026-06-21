## ADDED Requirements

### Requirement: INode defines unified node interface
系统 SHALL 定义 `INode` 抽象接口，所有处理节点（Source/Transform/Sink）必须实现。

INode SHALL 声明以下纯虚方法：
- `bool Configure(const NodeConfig& config)`：注入配置参数
- `bool Negotiate()`：声明/协商端口格式
- `bool Prepare()`：分配资源（codec/surface/device）
- `bool Start()`：启动工作线程（Active）或准备处理（Passive）
- `void Stop()`：停止并等待线程退出，释放 Prepared 资源
- `void Flush()`：清除内部状态（seek 时调用）
- `std::span<InputPort*> Inputs()` / `std::span<OutputPort*> Outputs()`：端口访问
- `NodeType Type()`：Source / Transform / Sink
- `ThreadingMode Threading()`：Active / Passive
- `NodeState State()`：当前状态

**Process 签名**（Transform 和 Sink 节点）：
```cpp
using OutputCallback = std::function<void(MediaBuffer)>;
virtual void Process(MediaBuffer input, OutputCallback emit) = 0;
```

Passive 节点在上游线程中被同步调用 Process()。使用回调而非返回值是因为：
- 某些滤镜可能从 1 帧产出 0 帧（如 framerate 丢帧）或多帧（如 interpolate）
- 避免 `std::vector<MediaBuffer>` 的堆分配开销
- 单帧场景下 emit 只被调用一次，无额外开销

#### Scenario: Node follows lifecycle sequence
- **WHEN** MediaGraph 调用节点的 Negotiate() → Prepare() → Start()
- **THEN** 节点的 State() 依次为 Idle → Configured → Prepared → Running

#### Scenario: Node transitions to Error state on failure
- **WHEN** Prepare() 中资源分配失败
- **THEN** State() 转为 Error，MediaGraph 停止后续节点初始化，记录错误日志

#### Scenario: Stop transitions from Running to Prepared
- **WHEN** 节点在 Running 状态收到 Stop()
- **THEN** 工作线程退出，资源保留，State() 转为 Prepared

### Requirement: NodeType categorizes node roles
系统 SHALL 定义 `enum class NodeType { kSource, kTransform, kSink }`。

- kSource：无输入端口，>=1 个输出端口，数据生产者
- kTransform：>=1 个输入端口，>=1 个输出端口，数据处理者
- kSink：>=1 个输入端口，无输出端口，数据消费者

#### Scenario: Source node has no inputs
- **WHEN** 查询 DemuxNode 的 Inputs()
- **THEN** 返回空 span

#### Scenario: Sink node has no outputs
- **WHEN** 查询 VideoSinkNode 的 Outputs()
- **THEN** 返回空 span

### Requirement: ThreadingMode selects execution strategy
系统 SHALL 定义 `enum class ThreadingMode { kPassive, kActive }`。

kPassive 节点：不拥有独立线程。其 `Process(MediaBuffer, OutputCallback)` 方法在上游 Active 节点的 Push 路径中被同步调用。数据不经过 Link 队列。回调 emit 可被调用 0 次（丢帧）、1 次（常规）或多次（插帧）。

kActive 节点：拥有独立工作线程。线程循环从输入端口 Pull 数据，处理后 Push 到输出端口。必须通过 Link 连接到相邻 Active 节点。

Source 节点必须是 Active。Sink 节点根据驱动方式选择：渲染器 Active（显示驱动），文件写入 Active（全速写）。

#### Scenario: Passive transform called on upstream thread
- **WHEN** Active DecoderNode Push 一个帧到输出端口，下游为 Passive AVFilterNode
- **THEN** AVFilterNode::Process() 在 DecoderNode 线程中被调用，不创建新线程，不经过 Link

#### Scenario: Active node has dedicated thread
- **WHEN** DecoderNode（Active）的 Start() 被调用
- **THEN** 创建内部工作线程，线程从输入端口 Pull 数据并解码

#### Scenario: Passive node can be chained
- **WHEN** Active 节点下游串联 Filter1(Passive) → Filter2(Passive) → Sink(Active)
- **THEN** Filter1::Process() → Filter2::Process() 依次在 Active 节点线程同步调用，最终结果入 Sink 的 Link

### Requirement: NodeState tracks lifecycle phase
系统 SHALL 定义 `enum class NodeState { kIdle, kConfigured, kPrepared, kRunning, kPaused, kError }`。

状态转换：
- Idle → Configured（Configure() 成功）
- Configured → Prepared（Prepare() 成功）
- Prepared → Running（Start()）
- Running → Paused（外部暂停）
- Running/Prepared → Idle（Stop() 后重置）
- 任意状态 → Error（操作失败）

#### Scenario: State transitions are sequential
- **WHEN** 节点从 Idle 尝试直接 Start()
- **THEN** 操作失败，State() 保持 Idle，记录警告日志

#### Scenario: Error state persists until reset
- **WHEN** 节点进入 Error 状态
- **THEN** 后续 Configure/Prepare/Start 调用均被拒绝，直到显式 Reset()
