## MODIFIED Requirements

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置最大字节数上限。默认上限 SHALL 为 15MB。

PacketQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 时 SHALL 将当前 serial 与 packet 一同入队。Pop 时 SHALL 通过 out 参数返回该 packet 的 serial。

`IncrementSerial()` SHALL 原子递增 serial 值，使后续 Push 的 packet 携带新 serial。已在队列中的旧 packet 保留其原始 serial，由消费方丢弃。

#### Scenario: Packets are dispatched to correct queues
- **WHEN** demux 线程读取到一个 audio packet
- **THEN** 该 packet 被推入 audio PacketQueue，携带当前 serial 值

#### Scenario: Demux blocks when queue is full
- **WHEN** 目标 PacketQueue 已达到最大字节数上限
- **THEN** demux 线程阻塞等待，直到队列字节数低于上限

#### Scenario: Serial increments on seek
- **WHEN** 调用 `IncrementSerial()`
- **THEN** serial 值递增，后续 Push 的 packet 携带新 serial

#### Scenario: Old packets carry old serial
- **WHEN** seek 后队列中仍有旧 packet
- **THEN** Pop 返回的旧 packet serial 值小于当前 serial，消费方可据此丢弃

## ADDED Requirements

### Requirement: FrameQueue supports serial
FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 时 SHALL 将当前 serial 与 frame 一同入队。Pop 时 SHALL 通过 out 参数返回该 frame 的 serial。

`IncrementSerial()` SHALL 原子递增 serial 值。

#### Scenario: Serial increments on seek
- **WHEN** 调用 `IncrementSerial()`
- **THEN** serial 值递增，后续 Push 的 frame 携带新 serial

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 frame serial 小于 render 线程记录的当前 serial
- **THEN** render 线程丢弃该 frame 并继续 Pop 下一帧

### Requirement: Decoder flushes codec on serial change
Decoder SHALL 记录上次处理的 packet serial。当 Pop 到一个 serial 大于 `last_serial_` 的 packet 时，Decoder SHALL 先调用 `avcodec_flush_buffers` 清空 codec 内部缓存，然后更新 `last_serial_` 并正常解码该 packet。

#### Scenario: Serial change triggers codec flush
- **WHEN** Decoder pop 到一个 packet 且其 serial > last_serial_
- **THEN** Decoder 执行 `avcodec_flush_buffers`，更新 last_serial_，然后解码该 packet

#### Scenario: Same serial does not flush
- **WHEN** Decoder pop 到一个 packet 且其 serial == last_serial_
- **THEN** Decoder 直接解码，不执行 flush

## REMOVED Requirements

### Requirement: Demux reaches end of file
**Reason**: 原有描述中提到"向各队列发送 flush packet 标记流结束"与实际实现不符（当前使用 break 退出循环）。EOF 处理不在本次重构范围内，保留实际行为（demux 线程退出）但移除不准确的 spec 描述，后续单独补充。
**Migration**: 无需迁移，实际行为不变。
