## MODIFIED Requirements

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置**最大字节数**上限。默认上限 SHALL 为 15MB（15 × 1024 × 1024 字节），与 FFplay `MAX_QUEUE_SIZE` 对齐。

Push 时 SHALL 累加 `pkt->size` 到内部字节计数器；Pop 时 SHALL 相应扣减；Flush 时 SHALL 清零。

PacketQueue SHALL 提供 `ByteSize()` 方法返回当前缓冲的总字节数。

#### Scenario: Concurrent push and pop
- **WHEN** 多线程同时对 PacketQueue 进行 push 和 pop 操作
- **THEN** 不出现数据竞争或崩溃

#### Scenario: PacketQueue blocks when byte limit reached
- **WHEN** PacketQueue 当前缓冲字节数 >= 最大字节数上限
- **THEN** Push 操作阻塞等待，直到有 packet 被消费释放字节空间

#### Scenario: ByteSize reflects actual usage
- **WHEN** 向空队列 Push 若干 packet 后查询 ByteSize()
- **THEN** 返回值等于所有已入队 packet 的 `pkt->size` 之和

### Requirement: Demux blocks when queue is full
（原属 "Demux thread produces packets" 的场景，此处更新判定条件）

#### Scenario: Demux blocks when PacketQueue byte limit reached
- **WHEN** 目标 PacketQueue 已达到最大字节数上限
- **THEN** demux 线程阻塞等待，直到队列字节数低于上限

### Requirement: Video FrameQueue sizing
Video FrameQueue 的默认最大帧数 SHALL 为 3（对齐 FFplay `VIDEO_PICTURE_QUEUE_SIZE`）。

#### Scenario: Video FrameQueue blocks at 3 frames
- **WHEN** Video FrameQueue 中已有 3 帧未被消费
- **THEN** 视频解码线程 Push 操作阻塞等待

### Requirement: Audio FrameQueue sizing
Audio FrameQueue 的默认最大帧数 SHALL 为 9（对齐 FFplay `SAMPLE_QUEUE_SIZE`）。

#### Scenario: Audio FrameQueue blocks at 9 frames
- **WHEN** Audio FrameQueue 中已有 9 帧未被消费
- **THEN** 音频解码线程 Push 操作阻塞等待
