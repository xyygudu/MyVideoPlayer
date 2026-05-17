## MODIFIED Requirements

### Requirement: FrameQueue supports serial
`FrameQueue` SHALL 为类模板 `FrameQueue<T>`，其中 `T` 为帧类型（`VideoFrame` 或 `AudioFrame`）。

FrameQueue SHALL 定义公共类型 `QueueEntry<T>`（包含 `T frame`、`int serial`、`bool eof`），作为队列的传输单元。

Push 接口 SHALL 接收 `QueueEntry<T>` 值参数，通过 move 语义获取 frame 所有权。

Pop 接口 SHALL 返回 `std::optional<QueueEntry<T>>`。返回 `nullopt` 表示队列已 abort。调用方通过 `QueueEntry<T>::eof` 判断是否为 EOF 标记。

`PushEof(int serial)` SHALL 推入一个 `eof=true` 的 QueueEntry（frame 为默认构造的空帧）。

FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方在 `QueueEntry<T>::serial` 中显式传入 serial 值。

接口 SHALL 分离为三个独立方法：
- `Flush()`：清空队列数据 + 递增 serial。不改变 abort 状态。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。
- `Reset()`：重置为初始状态。

FrameQueue SHALL 通过显式模板实例化（explicit instantiation）保持编译隔离，在 .cc 文件中实例化 `FrameQueue<VideoFrame>` 和 `FrameQueue<AudioFrame>`。

#### Scenario: Flush only clears data and increments serial
- **WHEN** 调用 FrameQueue<VideoFrame>::Flush()
- **THEN** 队列数据被清空，serial 递增，abort 状态不变

#### Scenario: Abort only signals termination
- **WHEN** 调用 FrameQueue<AudioFrame>::Abort()
- **THEN** abort=true，所有等待线程被唤醒

#### Scenario: Serial increments on Flush
- **WHEN** 调用 Flush()
- **THEN** serial 值递增

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 QueueEntry serial 不等于 packet queue 的当前 serial
- **THEN** render 线程丢弃该 entry 并继续 Pop 下一帧

#### Scenario: QueueEntry carries VideoFrame with PTS
- **WHEN** Decoder push 一个 QueueEntry<VideoFrame>
- **THEN** entry.frame.pts() 返回已换算为秒的 PTS 值，下游无需再次计算

### Requirement: Decoder outputs public frame types
Decoder SHALL 接收 `DecoderParams` 值类型参数（包含 `AVRational time_base` 和 `AVRational frame_rate`），在解码循环中直接构建 `VideoFrame` 或 `AudioFrame`。

Decoder 解码出每帧后 SHALL 执行：
1. 使用 `params_.time_base` 将 `AVFrame::pts` 换算为秒
2. 构建对应的公共帧类型（内部 `av_frame_ref` 复制引用）
3. 将帧包装为 `QueueEntry<T>` 并 push 到 FrameQueue

Decoder SHALL 不持有 `AVStream*`，仅通过 `DecoderParams` 获取所需元数据。

#### Scenario: Decoder outputs VideoFrame with PTS in seconds
- **WHEN** Decoder 解码出一帧 video（AVFrame::pts = 90000，time_base = 1/90000）
- **THEN** 构建的 VideoFrame::pts() == 1.0（秒）

#### Scenario: Decoder outputs AudioFrame with PTS in seconds
- **WHEN** Decoder 解码出一帧 audio（AVFrame::pts = 48000，time_base = 1/48000）
- **THEN** 构建的 AudioFrame::pts() == 1.0（秒）

#### Scenario: Decoder does not depend on AVStream at runtime
- **WHEN** Decoder 运行中
- **THEN** Decoder 不持有任何 AVStream 指针，仅使用初始化时传入的 DecoderParams 值

## REMOVED Requirements

### Requirement: FrameConverter converts AVFrame to public types
**Reason**: PTS 换算和帧构建逻辑迁移到 Decoder 内部，FrameConverter 不再有独立存在价值。
**Migration**: 使用 Decoder 直接输出的 VideoFrame/AudioFrame，无需在显示层调用转换。
