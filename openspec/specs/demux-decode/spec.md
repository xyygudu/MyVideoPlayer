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
系统 SHALL 在独立的 demux 线程中持续读取 packet，并将音频 packet 和视频 packet 分别推入对应的 PacketQueue。

#### Scenario: Packets are dispatched to correct queues
- **WHEN** demux 线程读取到一个 audio packet
- **THEN** 该 packet 被推入 audio PacketQueue

#### Scenario: Demux blocks when queue is full
- **WHEN** 目标 PacketQueue 已达到最大字节数上限
- **THEN** demux 线程阻塞等待，直到队列字节数低于上限

#### Scenario: Demux reaches end of file
- **WHEN** demux 线程读取到文件末尾（`av_read_frame` 返回 EOF）
- **THEN** 系统向各队列发送 flush packet 标记流结束

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
