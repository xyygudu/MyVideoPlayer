## ADDED Requirements

### Requirement: StreamContext aggregates pipeline components
系统 SHALL 定义 `struct StreamContext`，聚合 `PacketQueue`、`Decoder`、`FrameQueue` 三个组件，为音频和视频提供对称的管线封装。

#### Scenario: Audio and video use same structure
- **WHEN** PlayerImpl 创建音频和视频管线
- **THEN** 两者均使用 StreamContext 实例，结构对称

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
StreamContext SHALL 提供 `Start()` 和 `Stop()` 方法，管理 Decoder 线程的启动与停止。Start() 启动 Decoder 线程（从 packet_queue 读、向 frame_queue 写）。Stop() 停止 Decoder 线程。

#### Scenario: Start begins decoding
- **WHEN** 调用 StreamContext::Start()
- **THEN** Decoder 线程启动，开始从 packet_queue 消费

#### Scenario: Stop terminates decoding
- **WHEN** 调用 StreamContext::Stop()
- **THEN** Decoder 线程安全退出
