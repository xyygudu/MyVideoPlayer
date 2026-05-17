## MODIFIED Requirements

### Requirement: StreamContext aggregates pipeline components
系统 SHALL 定义 `class StreamContext`（非模板），聚合 `PacketQueue`、`std::unique_ptr<IDecoder>`、`FrameQueue<MediaFrame>` 三个组件，为任意媒体流提供统一的管线封装。StreamContext SHALL 为具体类（非模板），不感知流过它的媒体类型。

成员 SHALL 为 private，外部通过方法访问。

StreamContext 构造函数 SHALL 接收：
- `std::unique_ptr<IDecoder> decoder`：解码器实现（依赖注入）
- `int frame_queue_size`：帧队列最大容量
- `int64_t max_packet_bytes`：包队列最大字节数（默认 `sync::kDefaultMaxQueueBytes`）

#### Scenario: Audio and video use same class
- **WHEN** PlayerImpl 创建音频和视频管线
- **THEN** 两者均使用 StreamContext 实例（非模板），注入不同的 IDecoder 实现

#### Scenario: StreamContext does not know media type
- **WHEN** StreamContext 实例被创建和使用
- **THEN** StreamContext 不持有也不查询 MediaType，完全类型无关

### Requirement: StreamContext provides unified Flush
StreamContext SHALL 提供 `Flush()` 方法，依次调用内部 `packet_queue_.Flush()` 和 `frame_queue_.Flush()`，统一清理数据管线。

#### Scenario: Flush clears both queues
- **WHEN** 调用 StreamContext::Flush()
- **THEN** packet_queue 和 frame_queue 均被清空，serial 各自递增

### Requirement: StreamContext provides FlushAndDropUntil
StreamContext SHALL 提供 `FlushAndDropUntil(double pts)` 方法，作为 Seek 的原子操作：
1. 调用 `packet_queue_.Flush()`
2. 调用 `frame_queue_.Flush()`
3. 调用 `decoder_->SetDropUntilPts(pts)`

#### Scenario: FlushAndDropUntil combines flush and drop
- **WHEN** 调用 StreamContext::FlushAndDropUntil(5.0)
- **THEN** 两个队列被清空并递增 serial，decoder 的 drop_until_pts 被设为 5.0

#### Scenario: FlushAndDropUntil is atomic from caller perspective
- **WHEN** PlayerImpl 执行 Seek
- **THEN** 只需调用一个方法，不再分别访问 packet_queue、frame_queue、decoder

### Requirement: StreamContext provides unified Abort
StreamContext SHALL 提供 `Abort()` 方法，依次调用 `packet_queue_.Abort()`、`frame_queue_.Abort()` 和 `decoder_->Stop()`，统一终止管线。

#### Scenario: Abort stops all components
- **WHEN** 调用 StreamContext::Abort()
- **THEN** 队列发出 abort 信号，decoder 线程退出

### Requirement: StreamContext provides Reset
StreamContext SHALL 提供 `Reset()` 方法，依次调用 `packet_queue_.Reset()` 和 `frame_queue_.Reset()`，将队列恢复到初始状态。

#### Scenario: Reset restores queues to initial state
- **WHEN** 调用 StreamContext::Reset()
- **THEN** packet_queue 和 frame_queue 的 abort=false、serial=0、数据清空

### Requirement: StreamContext provides Start and Stop
StreamContext SHALL 提供 `Start()` 和 `Stop()` 方法。

`Start()` SHALL 调用 `decoder_->Start(&packet_queue_)` 启动解码线程。回调已在 OpenDecoder 阶段注入，Start() 不再涉及回调配置。

`Stop()` SHALL 调用 `decoder_->Stop()`。

#### Scenario: Start begins decoding
- **WHEN** 调用 StreamContext::Start()
- **THEN** Decoder 线程启动，解码出的 MediaFrame 被推入 frame_queue

#### Scenario: Stop terminates decoding
- **WHEN** 调用 StreamContext::Stop()
- **THEN** Decoder 线程安全退出

### Requirement: StreamContext provides PopFrame
StreamContext SHALL 提供 `std::optional<QueueEntry<MediaFrame>> PopFrame()` 方法，从内部 frame_queue 弹出帧。

#### Scenario: PopFrame returns decoded frame
- **WHEN** frame_queue 中有帧且调用 PopFrame()
- **THEN** 返回包含 MediaFrame 的 QueueEntry

#### Scenario: PopFrame blocks when empty
- **WHEN** frame_queue 为空且未 abort
- **THEN** PopFrame 阻塞直到有帧可用

#### Scenario: PopFrame returns nullopt on abort
- **WHEN** frame_queue 已 abort
- **THEN** PopFrame 返回 nullopt

### Requirement: StreamContext provides CurrentSerial
StreamContext SHALL 提供 `int CurrentSerial() const` 方法，返回 packet_queue 的当前 serial 值。用于消费者判断帧是否过期。

#### Scenario: CurrentSerial reflects latest serial
- **WHEN** 调用 Flush() 后查询 CurrentSerial()
- **THEN** 返回递增后的 serial 值

### Requirement: StreamContext provides queue accessors for wiring
StreamContext SHALL 提供 `PacketQueue* GetPacketQueue()` 和 `FrameQueue<MediaFrame>* GetFrameQueue()` 方法，用于组件间连线（Demuxer 写入 PacketQueue，AudioRenderer 读取 FrameQueue）。

#### Scenario: Demuxer writes to StreamContext's PacketQueue
- **WHEN** Demuxer::Start 需要 PacketQueue 指针
- **THEN** 通过 StreamContext::GetPacketQueue() 获取

#### Scenario: AudioRenderer reads from StreamContext's FrameQueue
- **WHEN** AudioRenderer::Start 需要 FrameQueue 指针
- **THEN** 通过 StreamContext::GetFrameQueue() 获取

### Requirement: StreamContext provides OpenDecoder
StreamContext SHALL 提供 `bool OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx = nullptr)` 方法。OpenDecoder SHALL：
1. 调用 `decoder_->Open(stream, hw_ctx)` 初始化解码器
2. 调用 `decoder_->SetFrameCallback(...)` 注入帧入队回调
3. 调用 `decoder_->SetEofCallback(...)` 注入 EOF 入队回调

配置在 Open 阶段一次性完成，后续 Start/Stop 不再重复设置。

#### Scenario: OpenDecoder initializes and configures the decoder
- **WHEN** 调用 StreamContext::OpenDecoder(video_stream, hw_ctx)
- **THEN** 内部 IDecoder 被初始化且回调已注入，返回 true 表示成功

#### Scenario: OpenDecoder failure does not set callbacks
- **WHEN** decoder_->Open() 返回 false
- **THEN** OpenDecoder 返回 false，不调用 SetFrameCallback/SetEofCallback
