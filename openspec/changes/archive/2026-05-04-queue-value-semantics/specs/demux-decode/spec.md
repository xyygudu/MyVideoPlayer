## MODIFIED Requirements

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置**最大字节数**上限。默认上限 SHALL 为 15MB（15 × 1024 × 1024 字节），与 FFplay `MAX_QUEUE_SIZE` 对齐。

PacketQueue SHALL 定义公共类型 `SerialPacket`（包含 `AVPacketPtr pkt` 和 `int serial`），作为队列的传输单元。

Push 接口 SHALL 接收 `SerialPacket` 值参数，通过 move 语义获取 packet 所有权。Push 时 SHALL 累加 `pkt->size` 到内部字节计数器。

Pop 接口 SHALL 返回 `std::optional<SerialPacket>`。返回 `nullopt` 表示队列已 abort。Pop 时 SHALL 相应扣减字节计数。

PacketQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方在 `SerialPacket::serial` 中显式传入 serial 值。Pop 返回的 `SerialPacket` 携带对应的 serial。

`Flush()` SHALL 在同一把锁内清空队列并原子递增 serial 值。

PacketQueue SHALL 提供 `ByteSize()` 方法返回当前缓冲的总字节数。

#### Scenario: Concurrent push and pop
- **WHEN** 多线程同时对 PacketQueue 进行 push 和 pop 操作
- **THEN** 不出现数据竞争或崩溃

#### Scenario: Pop returns nullopt on abort
- **WHEN** PacketQueue 被 Abort 且队列为空
- **THEN** Pop 返回 `std::nullopt`

#### Scenario: Push transfers ownership via move
- **WHEN** 调用方构造 `SerialPacket{std::move(pkt), serial}` 并 Push
- **THEN** 调用方的 `AVPacketPtr` 变为 moved-from 状态，队列持有数据所有权

### Requirement: FrameQueue is thread-safe
FrameQueue SHALL 定义公共类型 `SerialFrame`（包含 `AVFramePtr frame`、`int serial`、`bool eof`），作为队列的传输单元。

Push 接口 SHALL 接收 `SerialFrame` 值参数，通过 move 语义获取 frame 所有权。

Pop 接口 SHALL 返回 `std::optional<SerialFrame>`。返回 `nullopt` 表示队列已 abort。调用方通过 `SerialFrame::eof` 判断是否为 EOF 标记。

`PushEof(int serial)` SHALL 推入一个 `eof=true` 的 SerialFrame。

#### Scenario: Pop returns nullopt on abort
- **WHEN** FrameQueue 被 Abort 且队列为空
- **THEN** Pop 返回 `std::nullopt`

#### Scenario: EOF detection via SerialFrame
- **WHEN** Pop 返回的 `SerialFrame` 的 `eof` 字段为 true
- **THEN** 调用方识别为流结束标记
