## MODIFIED Requirements

### Requirement: StreamContext aggregates pipeline components
系统 SHALL 定义类模板 `StreamContext<FrameType>`，聚合 `PacketQueue`、`Decoder`、`FrameQueue<FrameType>` 三个组件，为音频和视频提供对称的管线封装。

`PlayerImpl` SHALL 持有 `StreamContext<VideoFrame>` 和 `StreamContext<AudioFrame>` 实例。

StreamContext SHALL 通过显式模板实例化保持编译隔离。

#### Scenario: Video uses StreamContext<VideoFrame>
- **WHEN** PlayerImpl 创建视频管线
- **THEN** 使用 `StreamContext<VideoFrame>` 实例，frame_queue 为 `FrameQueue<VideoFrame>`

#### Scenario: Audio uses StreamContext<AudioFrame>
- **WHEN** PlayerImpl 创建音频管线
- **THEN** 使用 `StreamContext<AudioFrame>` 实例，frame_queue 为 `FrameQueue<AudioFrame>`

### Requirement: StreamContext provides unified Flush
StreamContext SHALL 提供 `Flush()` 方法，依次调用 `packet_queue.Flush()` 和 `frame_queue.Flush()`，统一清理数据管线。

#### Scenario: Flush clears both queues
- **WHEN** 调用 StreamContext::Flush()
- **THEN** packet_queue 和 frame_queue 均被清空，serial 各自递增

### Requirement: StreamContext provides unified Abort
StreamContext SHALL 提供 `Abort()` 方法，依次调用 `packet_queue.Abort()`、`frame_queue.Abort()` 和 `decoder.Stop()`，统一终止管线。

#### Scenario: Abort stops all components
- **WHEN** 调用 StreamContext::Abort()
- **THEN** 队列发出 abort 信号，decoder 线程退出

### Requirement: StreamContext provides Start and Stop
StreamContext SHALL 提供 `Start()` 和 `Stop()` 方法，管理 Decoder 线程的启动与停止。Start() 启动 Decoder 线程（从 packet_queue 读、向 FrameQueue<FrameType> 写）。Stop() 停止 Decoder 线程。

#### Scenario: Start begins decoding
- **WHEN** 调用 StreamContext<VideoFrame>::Start()
- **THEN** Decoder 线程启动，解码输出 VideoFrame 到 FrameQueue<VideoFrame>

#### Scenario: Stop terminates decoding
- **WHEN** 调用 StreamContext::Stop()
- **THEN** Decoder 线程安全退出

### Requirement: StreamContext accepts DecoderParams at initialization
StreamContext SHALL 在 `OpenDecoder` 时接收 `DecoderParams`（含 `time_base` 和 `frame_rate`），传递给内部 Decoder。StreamContext 不持有 `AVStream*`。

#### Scenario: OpenDecoder receives DecoderParams
- **WHEN** PlayerImpl 调用 StreamContext::OpenDecoder(codecpar, params)
- **THEN** Decoder 使用 params.time_base 进行 PTS 换算，不依赖 AVStream
