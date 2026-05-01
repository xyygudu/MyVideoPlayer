## ADDED Requirements

### Requirement: Demuxer opens media file
系统 SHALL 能通过 FFmpeg 的 `avformat_open_input` 打开本地媒体文件，并读取流信息。

#### Scenario: Successfully open a valid video file
- **WHEN** 调用 `Player::Open` 并传入一个有效的本地视频文件路径
- **THEN** 系统成功打开文件，识别出音频流和视频流，返回 true

#### Scenario: Fail to open invalid file
- **WHEN** 调用 `Player::Open` 并传入一个不存在或损坏的文件路径
- **THEN** 系统返回 false，不崩溃

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

### Requirement: Audio decoder produces frames
系统 SHALL 从 audio PacketQueue 取出 packet，使用 FFmpeg 解码器解码为 PCM 音频帧。

#### Scenario: Decode audio packet to frame
- **WHEN** audio PacketQueue 中有可用的 audio packet
- **THEN** 系统将其送入 `avcodec_send_packet` / `avcodec_receive_frame` 解码为 AVFrame

### Requirement: Video decoder produces frames
系统 SHALL 从 video PacketQueue 取出 packet，使用 FFmpeg 解码为视频帧，并经 `sws_scale` 转为 RGB32 格式存入 FrameQueue。

#### Scenario: Decode video packet to RGB frame
- **WHEN** video PacketQueue 中有可用的 video packet
- **THEN** 系统解码为 AVFrame 并转换为 RGB32 格式放入 FrameQueue

#### Scenario: Video FrameQueue blocks when full
- **WHEN** FrameQueue 达到最大容量
- **THEN** 视频解码线程阻塞等待，直到有帧被消费

### Requirement: PacketQueue is thread-safe
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置**最大字节数**上限。默认上限 SHALL 为 15MB（15 × 1024 × 1024 字节），与 FFplay `MAX_QUEUE_SIZE` 对齐。

Push 时 SHALL 累加 `pkt->size` 到内部字节计数器；Pop 时 SHALL 相应扣减；FlushAndIncrementSerial 时 SHALL 清零。

PacketQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 packet 一同入队。Pop 时 SHALL 通过 out 参数返回该 packet 的 serial。

`FlushAndIncrementSerial()` SHALL 在同一把锁内清空队列并原子递增 serial 值。

PacketQueue SHALL 提供 `ByteSize()` 方法返回当前缓冲的总字节数。

#### Scenario: Concurrent push and pop
- **WHEN** 多线程同时对 PacketQueue 进行 push 和 pop 操作
- **THEN** 不出现数据竞争或崩溃

#### Scenario: PacketQueue blocks when byte limit reached
- **WHEN** PacketQueue 当前缓冲字节数 >= 最大字节数上限
- **THEN** Push 操作阻塞等待，直到有 packet 被消费释放字节空间

#### Scenario: Serial increments on seek
- **WHEN** 调用 `FlushAndIncrementSerial()`
- **THEN** 队列被清空，serial 值递增，后续 Push 的 packet 可携带新 serial

#### Scenario: Old packets carry old serial
- **WHEN** seek 后 Pop 到队列中残留的旧 packet（flush 前入队或 demux 线程 seek 处理前推入）
- **THEN** Pop 返回的旧 packet serial 值小于当前 serial，消费方据此丢弃

#### Scenario: ByteSize reflects actual usage
- **WHEN** 向空队列 Push 若干 packet 后查询 ByteSize()
- **THEN** 返回值等于所有已入队 packet 的 `pkt->size` 之和

### Requirement: FrameQueue supports serial
FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方显式传入 serial 值与 frame 一同入队。Pop 时 SHALL 通过 out 参数返回该 frame 的 serial。

`FlushAndIncrementSerial()` SHALL 在同一把锁内清空队列并原子递增 serial 值。

#### Scenario: Serial increments on seek
- **WHEN** 调用 `FlushAndIncrementSerial()`
- **THEN** 队列被清空，serial 值递增

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 frame serial 不等于 packet queue 的当前 serial
- **THEN** render 线程丢弃该 frame 并继续 Pop 下一帧

### Requirement: Decoder flushes codec on serial change
Decoder SHALL 记录上次处理的 packet serial（`last_serial_`）。当 Pop 到一个 serial 不等于 `last_serial_` 的 packet 时，Decoder SHALL 先调用 `avcodec_flush_buffers` 清空 codec 内部缓存，然后更新 `last_serial_` 并正常解码该 packet。Decoder Push frame 时 SHALL 传入 `last_serial_` 作为 frame 的 serial。

#### Scenario: Serial change triggers codec flush
- **WHEN** Decoder pop 到一个 packet 且其 serial != last_serial_
- **THEN** Decoder 执行 `avcodec_flush_buffers`，更新 last_serial_，然后解码该 packet

#### Scenario: Same serial does not flush
- **WHEN** Decoder pop 到一个 packet 且其 serial == last_serial_
- **THEN** Decoder 直接解码，不执行 flush

### Requirement: FrameQueue is thread-safe
FrameQueue SHALL 使用 mutex + condition_variable 实现线程安全，限制最大帧数。

#### Scenario: Concurrent write and read
- **WHEN** 解码线程 push frame 且渲染逻辑同时 pop frame
- **THEN** 不产生数据竞争，操作正确完成

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
