## ADDED Requirements

### Requirement: InputPort::TryPull 非阻塞取数
`InputPort` SHALL 提供 `TryPull()`：与 `Pull()` 使用同一套过滤（丢弃旧世代与畸形缓冲），但链路中没有可用缓冲时立即返回空而不阻塞；底层 `Link::TryPop()` 与 `Pop()` 共用出队逻辑（字节计数与唤醒生产者）。多输入的 Active 节点 SHALL 用它轮询"非主"输入。

#### Scenario: 叠加输入为空
- **WHEN** VideoCompositorNode 处理主帧时摄像头链路为空
- **THEN** `TryPull()` 立即返回空，主帧照常输出

#### Scenario: 过滤旧世代
- **WHEN** 链路中只有旧 seek 世代的缓冲
- **THEN** `TryPull()` 丢弃它们并返回空

### Requirement: MessageBus 带超时等待
`MessageBus` SHALL 提供 `WaitFor(timeout)`：有消息时返回消息；超时或已关闭时返回空，二者以 `IsShutDown()` 区分。facade 的事件线程 SHALL 可借此在等待消息的同时检查自己的截止时间。

#### Scenario: 停止超时
- **WHEN** Recorder 停止后 15 秒仍未收到 MuxNode 的 kEos
- **THEN** 事件线程在某次 `WaitFor` 超时后发现截止时间已过，以失败结束录制
