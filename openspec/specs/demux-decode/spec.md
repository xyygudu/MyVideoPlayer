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
- **WHEN** 目标 PacketQueue 已达到最大容量
- **THEN** demux 线程阻塞等待，直到队列有空间

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
PacketQueue SHALL 使用 mutex + condition_variable 实现线程安全的 push/pop 操作，并支持设置最大容量。

#### Scenario: Concurrent push and pop
- **WHEN** demux 线程 push packet 且解码线程同时 pop packet
- **THEN** 不产生数据竞争，操作正确完成

### Requirement: FrameQueue is thread-safe
FrameQueue SHALL 使用 mutex + condition_variable 实现线程安全，限制最大帧数。

#### Scenario: Concurrent write and read
- **WHEN** 解码线程 push frame 且渲染逻辑同时 pop frame
- **THEN** 不产生数据竞争，操作正确完成
