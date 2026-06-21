## ADDED Requirements

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
- `ByteCapacity`：基于字节数（用于压缩包流，max_bytes 默认 `sync::kDefaultMaxQueueBytes`）
- `CountCapacity`：基于帧数（用于解码帧流，max_count 默认 4）

Link SHALL 提供：
- `Push(MediaBuffer)`：阻塞直到容量可用或被 abort。Push 时 SHALL 将当前 Link 的 serial_ 值 stamp 到 `buf.serial`，确保数据携带版本标记。
- `Pop()` → `std::optional<MediaBuffer>`：阻塞直到数据可用或被 abort
- `Flush()`：清空队列 + 原子递增 serial_（过期帧通过 serial 比对被丢弃）
- `Abort()`：唤醒所有等待线程，但不清理数据
- `Reset()`：恢复初始状态
- `serial()` → `int`：返回当前 serial 值（原子读取）

**Serial 所有权规则**：
- Link 拥有并维护 `serial_` 计数器
- `Push()` 负责将 `serial_` 的当前值写入 `MediaBuffer::serial`
- 消费端 `Pop()` 后，通过比对 `buf.serial` 与节点自身的 `expected_serial_`（Flush 时递增）来丢弃过期帧
- Flush() 同时递增 Link 的 serial_ 和下游节点的 expected_serial_

线程安全：mutex + 两个条件变量（push/pop 分离），serial 为 `std::atomic<int>`。

#### Scenario: CountCapacity blocks on full queue
- **WHEN** CountCapacity(3) 的 Link 已满 3 帧，上游 Push 第 4 帧
- **THEN** Push 阻塞，直到下游 Pop 后腾出空间

#### Scenario: ByteCapacity limits total bytes
- **WHEN** ByteCapacity(15MB) 的 Link 累计 14MB 数据，Push 一个 2MB 包
- **THEN** Push 阻塞，直到 Pop 后腾出足够字节

#### Scenario: Flush increments serial and clears queue
- **WHEN** 调用 Link::Flush()
- **THEN** 队列清空，Link 的 serial_ 递增 1，后续 Push 的 MediaBuffer 携带新 serial 值

#### Scenario: Consumer discards stale buffers via serial
- **WHEN** Flush 后 serial_ 变为 5，但下游节点之前缓存了 serial=4 的 MediaBuffer
- **THEN** 节点比对 buf.serial < expected_serial_，丢弃该过期帧

#### Scenario: Abort wakes blocked threads
- **WHEN** 调用 Link::Abort() 时 Pop() 正在阻塞
- **THEN** Pop() 返回 nullopt，调用方退出等待循环

### Requirement: Port provides node connection endpoint
系统 SHALL 定义 `InputPort` 和 `OutputPort` 类，作为节点的数据接入/输出点。

OutputPort SHALL 提供：
- `Connect(InputPort* peer)`：建立连接，内部创建 Link
- `Push(MediaBuffer)`：发送数据到下游。路由策略：
  - 如果下游节点为 Passive：同步调用 `downstream->Process(buf, output_callback)`，回调将结果传递到下一个节点
  - 如果下游节点为 Active：数据入 Link 队列
- `SetCaps(FormatCaps)`：设置支持的输出格式范围

InputPort SHALL 提供：
- `Pull()` → `std::optional<MediaBuffer>`：从上游 Link 拉取数据
- `SetCaps(FormatCaps)`：设置支持的输入格式范围
- `peer()` / `link()`：访问对端端口和 Link

Connect SHALL 取输出 Caps 和输入 Caps 的交集验证。端口 SHALL 返回其所属节点（`owner()`）。

#### Scenario: Connect two Active nodes creates Link
- **WHEN** DemuxNode OutputPort 连接到 DecoderNode InputPort
- **THEN** 内部创建 ByteCapacity Link，格式协商记录到 Link

#### Scenario: Connect two nodes with compatible formats
- **WHEN** OutputPort caps={YUV420P, NV12} 连接到 InputPort caps={YUV420P, RGB32}
- **THEN** 协商成功，Link 记录格式为 YUV420P（取交集）

#### Scenario: Connect incompatible formats fails
- **WHEN** OutputPort caps={YUV420P} 连接到 InputPort caps={RGB32}
- **THEN** Connect 返回 false，不创建 Link

### Requirement: MediaGraph manages node topology and lifecycle
系统 SHALL 定义 `MediaGraph` 类，作为图中所有节点和连接的容器和管理器。

MediaGraph SHALL 提供：
- `AddNode(unique_ptr<INode>)` → `INode*`：添加节点
- `Connect(OutputPort*, InputPort*)` → `bool`：连接两个端口
- `Negotiate()` → `bool`：按拓扑序执行格式协商，任一失败返回 false
- `Prepare()` → `bool`：按拓扑序分配资源
- `Start()`：启动所有 Active 节点线程
- `Stop()`：停止所有节点线程
- `Flush()`：广播 Flush 到所有节点和 Link
- `SetClock(shared_ptr<IClock>)` / `Clock()`：全局时钟管理
- `SetEventCallback(function<void(GraphEvent)>)`：事件通知

拓扑排序：确保 Source→Transform→Sink 的依赖顺序，DAG 环路检测。

#### Scenario: Playback graph topology sort
- **WHEN** 构建 DemuxNode→Decoder→Sink 图
- **THEN** 拓扑排序结果为 DemuxNode, Decoder, Sink

#### Scenario: Cycle detection rejects invalid graph
- **WHEN** 构建 A→B→C→A 的环图
- **THEN** Negotiate() 检测到环并返回 false，spdlog 报告环路节点

#### Scenario: Graph broadcasts Flush on seek
- **WHEN** 调用 MediaGraph::Flush()
- **THEN** 所有 Link 清空并 serial++，所有节点内部状态清除

#### Scenario: Graph reports EOS event
- **WHEN** 所有 Sink 节点报告 EOF
- **THEN** EventCallback 收到 GraphEvent::kEos，Graph 状态变为 Finished
