## MODIFIED Requirements

### Requirement: Video decoder produces frames
系统 SHALL 从 video PacketQueue 取出 packet，使用 FFmpeg 解码为视频帧，直接以原始像素格式（通常为 YUV420P）存入 FrameQueue。解码器 SHALL 不再进行 CPU 端像素格式转换。

#### Scenario: Decode video packet to native format frame
- **WHEN** video PacketQueue 中有可用的 video packet
- **THEN** 系统解码为 AVFrame 并以原始像素格式放入 FrameQueue（不做 sws_scale 转换）

#### Scenario: Video FrameQueue blocks when full
- **WHEN** FrameQueue 达到最大容量
- **THEN** 视频解码线程阻塞等待，直到有帧被消费

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置**最大字节数**上限。默认上限 SHALL 为 15MB（15 × 1024 × 1024 字节），与 FFplay `MAX_QUEUE_SIZE` 对齐。

PacketQueue 内部 SHALL 使用 `AVPacketPtr`（RAII 包装）管理 packet 生命周期。Flush 时队列中所有 packet SHALL 通过 RAII 析构自动释放，不需要手动调用 `av_packet_free`。

Push 时 SHALL 累加 `pkt->size` 到内部字节计数器；Pop 时 SHALL 相应扣减；Flush 时 SHALL 清零。

PacketQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 packet 一同入队。Pop 时 SHALL 通过 out 参数返回该 packet 的 serial。

`Flush()` SHALL 在同一把锁内清空队列并递增 serial 值。

PacketQueue SHALL 提供 `ByteSize()` 方法返回当前缓冲的总字节数。

#### Scenario: Concurrent push and pop
- **WHEN** 多线程同时对 PacketQueue 进行 push 和 pop 操作
- **THEN** 不出现数据竞争或崩溃

#### Scenario: PacketQueue blocks when byte limit reached
- **WHEN** PacketQueue 当前缓冲字节数 >= 最大字节数上限
- **THEN** Push 操作阻塞等待，直到有 packet 被消费释放字节空间

#### Scenario: Serial increments on seek
- **WHEN** 调用 `Flush()`
- **THEN** 队列被清空（RAII 自动释放所有 packet），serial 值递增

#### Scenario: Old packets carry old serial
- **WHEN** seek 后 Pop 到队列中残留的旧 packet
- **THEN** Pop 返回的旧 packet serial 值小于当前 serial，消费方据此丢弃

#### Scenario: ByteSize reflects actual usage
- **WHEN** 向空队列 Push 若干 packet 后查询 ByteSize()
- **THEN** 返回值等于所有已入队 packet 的 `pkt->size` 之和

### Requirement: FrameQueue supports serial
FrameQueue 内部 SHALL 使用 `AVFramePtr`（RAII 包装）管理 frame 生命周期。Flush 时队列中所有 frame SHALL 通过 RAII 析构自动释放。

FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 frame 一同入队。Pop 时 SHALL 通过 out 参数返回该 frame 的 serial。

`Flush()` SHALL 在同一把锁内清空队列并递增 serial 值。

#### Scenario: Serial increments on seek
- **WHEN** 调用 `Flush()`
- **THEN** 队列被清空（RAII 自动释放所有 frame），serial 值递增

#### Scenario: RAII prevents frame leaks on flush
- **WHEN** FrameQueue 中有多帧数据时调用 Flush
- **THEN** 所有帧通过 AVFramePtr 析构自动释放，无内存泄漏
