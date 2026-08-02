## MODIFIED Requirements

### Requirement: MediaBuffer unifies pipeline data carrier
系统 SHALL 定义 `MediaBuffer` 类型，作为图中所有节点间传输的统一数据载体。payload SHALL 为 `std::variant<AVPacketPtr, MediaFrame>`，禁止使用 void* 或 type-erasure。

MediaBuffer SHALL 携带以下元数据：
- `Timestamp timestamp`：`pts`（秒）与 `time_base`。`time_base` 仅对包载荷有意义 —— 用于把 AVPacket 原生的 pts/dts 换算到目标流；帧载荷的 pts 已是秒制
- `BufferFlags flags`：位标记（kEos, kDiscontinuity, kKeyFrame, kCorrupt）
- `int serial`：seek 世代标记，用于识别过期在途数据

MediaBuffer SHALL NOT 携带媒体类型：媒体类型是**链路的属性**而非单个 buffer 的属性，同一条 Link 上流过的 buffer 类型必然相同，权威来源是协商期确定的 `InputPort::Format()`。把已协商的状态复制进数据流 SHALL 被避免。

`Timestamp` SHALL NOT 包含无读者的派生字段。秒制的 dts 与 duration SHALL NOT 存在 —— 需要时数据在 AVPacket / AVFrame 内。

两个构造函数 SHALL 形状一致，仅 payload 类型不同。`MakeEos(int serial)` SHALL 不接收媒体类型 —— EOS 送往哪一路由输出端口的选择决定。

MediaBuffer SHALL 为 move-only 语义。`IsPacket()` / `IsFrame()` / `AsPacket()` / `AsFrame()` 提供类型安全访问。

#### Scenario: Packet payload flows through Link
- **WHEN** DemuxNode 产生一个 AVPacket 并包装为 MediaBuffer Push 到下游 Link
- **THEN** DecoderNode 从 Link Pop 后可通过 `AsPacket()` 获取 AVPacket，`IsFrame()` 返回 false

#### Scenario: Frame payload flows through Link
- **WHEN** DecoderNode 解码出 MediaFrame 并包装为 MediaBuffer Push 到下游 Link
- **THEN** 下游节点通过 `AsFrame()` 获取 MediaFrame，`IsPacket()` 返回 false

#### Scenario: 媒体类型来自端口而非 buffer
- **WHEN** 某节点需要知道输入数据是音频还是视频
- **THEN** 它读取 `input_port_->Format().media_type()`，而非从 buffer 上取

#### Scenario: EOS flag marks end of stream
- **WHEN** Source 节点到达文件末尾
- **THEN** Push 一个 `flags_ & kEos` 为 true 的 MediaBuffer，下游节点据此停止拉取

#### Scenario: Serial detects stale frame after seek
- **WHEN** Seek 后 Flush() 递增 serial 为 5，但队列中仍有 serial=4 的残留 MediaBuffer
- **THEN** 输入端口在 Pull 时比对 serial，丢弃过期数据

#### Scenario: Move semantics transfer ownership
- **WHEN** MediaBuffer 被 move 到另一个对象
- **THEN** 源对象 payload 变为空 variant，目标对象持有完整数据

#### Scenario: 构造不读取已移动的对象
- **WHEN** 以 MediaFrame 构造 MediaBuffer
- **THEN** 构造函数 SHALL NOT 从被移动的载荷上读取任何元数据
