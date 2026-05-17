## MODIFIED Requirements

### Requirement: FrameQueue supports serial
FrameQueue SHALL 为模板类 `FrameQueue<T>`，管线中 SHALL 实例化为 `FrameQueue<MediaFrame>`。

QueueEntry 定义 SHALL 为：
```cpp
template<typename T>
struct QueueEntry {
    T frame;
    int serial;
    bool eof{false};
};
```

Push 接口 SHALL 接收 `QueueEntry<T>` 值参数，通过 move 语义获取 frame 所有权。

Pop 接口 SHALL 返回 `std::optional<QueueEntry<T>>`。返回 `nullopt` 表示队列已 abort。调用方通过 `QueueEntry::eof` 判断是否为 EOF 标记。

`PushEof(int serial)` SHALL 推入一个 `eof=true` 的 QueueEntry（frame 为默认构造）。

FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方在 `QueueEntry::serial` 中显式传入 serial 值。

接口 SHALL 分离为三个独立方法：
- `Flush()`：清空队列数据 + 递增 serial。不改变 abort 状态。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。
- `Reset()`：重置为初始状态。

#### Scenario: Flush only clears data and increments serial
- **WHEN** 调用 FrameQueue<MediaFrame>::Flush()
- **THEN** 队列数据被清空，serial 递增，abort 状态不变

#### Scenario: Abort only signals termination
- **WHEN** 调用 FrameQueue<MediaFrame>::Abort()
- **THEN** abort=true，所有等待线程被唤醒

#### Scenario: Serial increments on Flush
- **WHEN** 调用 Flush()
- **THEN** serial 值递增

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 QueueEntry serial 不等于 StreamContext::CurrentSerial()
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

## ADDED Requirements

### Requirement: Demuxer provides typed stream accessors
Demuxer SHALL 提供以下访问器方法，替代 `FormatContext()` 的公开暴露：
- `AVStream* AudioStream() const`：返回音频流指针（无音频时返回 nullptr）
- `AVStream* VideoStream() const`：返回视频流指针（无视频时返回 nullptr）

Demuxer SHALL 不再公开 `FormatContext()` 方法。内部需要 `AVFormatContext` 的操作 SHALL 在 Demuxer 内部完成。

#### Scenario: AudioStream returns valid pointer
- **WHEN** 文件包含音频流且 Demuxer 已 Open
- **THEN** AudioStream() 返回有效的 AVStream*

#### Scenario: VideoStream returns nullptr for audio-only file
- **WHEN** 文件不包含视频流
- **THEN** VideoStream() 返回 nullptr

#### Scenario: FormatContext is not publicly accessible
- **WHEN** 外部代码尝试访问 Demuxer 的 FormatContext
- **THEN** 编译失败（方法为 private 或已移除）

## REMOVED Requirements

### Requirement: Decoder flushes codec on serial change
**Reason**: Decoder 类被重构为 AVFrameDecoder（实现 IDecoder 接口）。serial change flush 行为保留，但在 decoder-interface spec 的 AVFrameDecoder 要求中定义。
**Migration**: 参见 decoder-interface spec 中的 "AVFrameDecoder flushes on serial change" scenario。

### Requirement: Decoder supports skip_frame during seek
**Reason**: 同上，行为保留但定义迁移到 decoder-interface spec。
**Migration**: 参见 decoder-interface spec 中 AVFrameDecoder 的相关 scenarios。

### Requirement: Decoder drops frames before target pts
**Reason**: 同上，SetDropUntilPts 现在是 IDecoder 接口的一部分。
**Migration**: 参见 decoder-interface spec 中 "AVFrameDecoder drops frames before target pts" scenario。
