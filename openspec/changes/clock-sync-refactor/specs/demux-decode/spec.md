## MODIFIED Requirements

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置最大字节数上限。默认上限 SHALL 为 `kDefaultMaxQueueBytes`（15 × 1024 × 1024 字节）。

Push 时 SHALL 累加 `pkt->size` 到内部字节计数器；Pop 时 SHALL 相应扣减；Flush 时 SHALL 清零。

PacketQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 packet 一同入队。Pop 时 SHALL 通过 out 参数返回该 packet 的 serial。

接口 SHALL 分离为三个独立方法：
- `Flush()`：清空队列数据 + 递增 serial。不改变 abort 状态。用于 Seek。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。用于 Stop/Close。
- `Reset()`：重置为初始状态（abort=false, serial=0, 清空数据）。用于 Close 后复用。

PacketQueue SHALL 提供 `ByteSize()` 方法返回当前缓冲的总字节数。

#### Scenario: Flush only clears data and increments serial
- **WHEN** 调用 Flush()
- **THEN** 队列数据被清空，serial 递增，abort 状态不变，Push/Pop 仍可正常工作

#### Scenario: Abort only signals termination
- **WHEN** 调用 Abort()
- **THEN** abort=true，所有等待线程被唤醒，队列数据不被清空

#### Scenario: Reset restores initial state
- **WHEN** 调用 Reset()
- **THEN** abort=false, serial=0, 数据被清空，队列可再次正常使用

#### Scenario: Concurrent push and pop
- **WHEN** 多线程同时对 PacketQueue 进行 push 和 pop 操作
- **THEN** 不出现数据竞争或崩溃

#### Scenario: PacketQueue blocks when byte limit reached
- **WHEN** PacketQueue 当前缓冲字节数 >= 最大字节数上限
- **THEN** Push 操作阻塞等待，直到有 packet 被消费释放字节空间

#### Scenario: Serial increments on Flush
- **WHEN** 调用 Flush()
- **THEN** serial 值递增，后续 Push 的 packet 可携带新 serial

#### Scenario: ByteSize reflects actual usage
- **WHEN** 向空队列 Push 若干 packet 后查询 ByteSize()
- **THEN** 返回值等于所有已入队 packet 的 `pkt->size` 之和

### Requirement: FrameQueue supports serial
FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 frame 一同入队。Pop 时 SHALL 通过 out 参数返回该 frame 的 serial。

FrameQueue 的 SerialFrame 结构 SHALL 包含 `bool eof` 字段（默认 false）。

接口 SHALL 分离为三个独立方法：
- `Flush()`：清空队列数据 + 递增 serial。不改变 abort 状态。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。
- `Reset()`：重置为初始状态。

#### Scenario: Flush only clears data and increments serial
- **WHEN** 调用 FrameQueue::Flush()
- **THEN** 队列数据被清空，serial 递增，abort 状态不变

#### Scenario: Abort only signals termination
- **WHEN** 调用 FrameQueue::Abort()
- **THEN** abort=true，所有等待线程被唤醒

#### Scenario: Serial increments on Flush
- **WHEN** 调用 Flush()
- **THEN** serial 值递增

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 frame serial 不等于 packet queue 的当前 serial
- **THEN** render 线程丢弃该 frame 并继续 Pop 下一帧

### Requirement: Demux thread produces packets
系统 SHALL 在独立的 demux 线程中持续读取 packet，并将音频 packet 和视频 packet 分别推入对应的 PacketQueue。Demuxer SHALL 维护本地 serial 副本，仅在 seek 完成后更新为最新值，确保 seek 前的旧 packet 保留旧 serial。

#### Scenario: Packets are dispatched to correct queues
- **WHEN** demux 线程读取到一个 audio packet
- **THEN** 该 packet 被推入 audio PacketQueue，携带当前本地 serial 值

#### Scenario: Demux blocks when queue is full
- **WHEN** 目标 PacketQueue 已达到最大字节数上限
- **THEN** demux 线程阻塞等待，直到队列字节数低于上限

#### Scenario: Demux updates serial after seek
- **WHEN** demux 线程处理完 seek 请求（av_seek_frame 返回后）
- **THEN** demux 更新本地 serial 副本为 packet queue 的最新 serial 值

### Requirement: Decoder flushes codec on serial change
Decoder SHALL 记录上次处理的 packet serial（`last_serial_`）。当 Pop 到一个 serial 不等于 `last_serial_` 的 packet 时，Decoder SHALL 先调用 `avcodec_flush_buffers` 清空 codec 内部缓存，然后更新 `last_serial_` 并正常解码该 packet。Decoder Push frame 时 SHALL 传入 `last_serial_` 作为 frame 的 serial。

#### Scenario: Serial change triggers codec flush
- **WHEN** Decoder pop 到一个 packet 且其 serial != last_serial_
- **THEN** Decoder 执行 `avcodec_flush_buffers`，更新 last_serial_，然后解码该 packet

#### Scenario: Same serial does not flush
- **WHEN** Decoder pop 到一个 packet 且其 serial == last_serial_
- **THEN** Decoder 直接解码，不执行 flush

### Requirement: Video FrameQueue sizing
Video FrameQueue 的默认最大帧数 SHALL 为 `kDefaultVideoQueueSize`（值为 3）。

#### Scenario: Video FrameQueue blocks at capacity
- **WHEN** Video FrameQueue 中帧数达到 kDefaultVideoQueueSize
- **THEN** 视频解码线程 Push 操作阻塞等待

### Requirement: Audio FrameQueue sizing
Audio FrameQueue 的默认最大帧数 SHALL 为 `kDefaultAudioQueueSize`（值为 9）。

#### Scenario: Audio FrameQueue blocks at capacity
- **WHEN** Audio FrameQueue 中帧数达到 kDefaultAudioQueueSize
- **THEN** 音频解码线程 Push 操作阻塞等待
