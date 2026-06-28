## REMOVED Requirements

*This spec has been superseded by the MediaGraph architecture. All requirements below are removed.*

### Requirement: IDecoder defines abstract decoder interface
**Removed.** Replaced by `DecoderNode` implementing `INode`. Lifecycle: Negotiate/Prepare/Start/Stop. Data output: OutputPort::Push().

### Requirement: IDecoder provides SetFrameCallback setter
**Removed.** Callback pattern replaced by OutputPort::Push() push model.

### Requirement: IDecoder provides SetEofCallback setter
**Removed.** EOF signaled via `BufferFlags::kEos` on MediaBuffer.

### Requirement: IDecoder acquires PacketQueue via Start
**Removed.** Data source is now InputPort + Link.

### Requirement: MediaFrameCallback signature outputs MediaFrame
**Removed.** Replaced by OutputPort::Push(MediaBuffer{frame}).

### Requirement: AVFrameDecoder implements IDecoder
**Removed.** Replaced by `DecoderNode : INode`.

### Requirement: EofOutputCallback remains unchanged
**Removed.** Replaced by kEos flag on MediaBuffer.

IDecoder 实现 SHALL 在解码出帧时通过此回调输出 MediaFrame（携带正确的 MediaType、pts）。

#### Scenario: Callback delivers MediaFrame with correct type
- **WHEN** AVFrameDecoder 解码出一帧视频
- **THEN** 回调被调用，传入的 MediaFrame 中 type()==MediaType::kVideo，pts 为正确的秒数

#### Scenario: Callback delivers audio frame
- **WHEN** AVFrameDecoder 解码出一帧音频
- **THEN** 回调被调用，传入的 MediaFrame 中 type()==MediaType::kAudio

### Requirement: AVFrameDecoder implements IDecoder
系统 SHALL 提供 `AVFrameDecoder` 类实现 `IDecoder` 接口。AVFrameDecoder SHALL：
- 使用 FFmpeg 的 `avcodec_send_packet` / `avcodec_receive_frame` API 进行解码
- 在 `Open()` 时从 `AVStream::codecpar->codec_type` 确定 MediaType 并缓存
- 通过 `SetFrameCallback` 和 `SetEofCallback` 接收回调并存储为成员变量
- 在 `Start()` 启动解码线程前 assert 回调已设置
- 在解码出帧时构造 `MediaFrame(raw_frame, pts, media_type_)` 并通过存储的回调输出
- 保持现有的 serial 变更检测、codec flush、skip_frame、drop_until_pts 等行为不变

#### Scenario: AVFrameDecoder decodes video stream
- **WHEN** AVFrameDecoder 以视频 AVStream 调用 Open()，SetFrameCallback，SetEofCallback，然后 Start()
- **THEN** 解码线程启动，持续从 PacketQueue 消费，输出 MediaFrame(type=kVideo)

#### Scenario: AVFrameDecoder decodes audio stream
- **WHEN** AVFrameDecoder 以音频 AVStream 调用 Open()，设置回调，然后 Start()
- **THEN** 解码线程启动，输出 MediaFrame(type=kAudio)

#### Scenario: AVFrameDecoder supports hardware acceleration
- **WHEN** Open() 时传入有效的 HWAccelContext
- **THEN** 解码使用硬件加速，输出的 MediaFrame 内部 AVFrame 为硬件帧格式

#### Scenario: AVFrameDecoder flushes on serial change
- **WHEN** 从 PacketQueue pop 到 serial 变更的 packet
- **THEN** AVFrameDecoder 执行 avcodec_flush_buffers，行为与原 Decoder 一致

#### Scenario: AVFrameDecoder drops frames before target pts
- **WHEN** SetDropUntilPts(5.0) 被调用且解码出帧 pts < 5.0
- **THEN** 该帧不通过回调输出，直接 unref

### Requirement: EofOutputCallback remains unchanged
EOF 回调签名 SHALL 保持为 `std::function<void(int serial)>`，IDecoder 实现在流结束时调用。

#### Scenario: EOF callback invoked at stream end
- **WHEN** Decoder 检测到解码流结束（receive_frame 返回 AVERROR_EOF）
- **THEN** 调用 EofOutputCallback 传入当前 serial
