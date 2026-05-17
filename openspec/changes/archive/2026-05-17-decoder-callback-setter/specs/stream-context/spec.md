## MODIFIED Requirements

### Requirement: StreamContext provides Start and Stop
StreamContext SHALL 提供 `Start()` 和 `Stop()` 方法。

`Start()` SHALL 调用 `decoder_->Start(&packet_queue_)` 启动解码线程。回调已在 OpenDecoder 阶段注入，Start() 不再涉及回调配置。

`Stop()` SHALL 调用 `decoder_->Stop()`。

#### Scenario: Start begins decoding
- **WHEN** 调用 StreamContext::Start()（回调已在 OpenDecoder 中设置）
- **THEN** Decoder 线程启动，解码出的 MediaFrame 被推入 frame_queue

#### Scenario: Stop terminates decoding
- **WHEN** 调用 StreamContext::Stop()
- **THEN** Decoder 线程安全退出

### Requirement: StreamContext provides OpenDecoder
StreamContext SHALL 提供 `bool OpenDecoder(AVStream* stream, HWAccelContext* hw_ctx = nullptr)` 方法。OpenDecoder SHALL：
1. 调用 `decoder_->Open(stream, hw_ctx)` 初始化解码器
2. 调用 `decoder_->SetFrameCallback(...)` 注入帧入队回调
3. 调用 `decoder_->SetEofCallback(...)` 注入 EOF 入队回调

配置在 Open 阶段一次性完成，后续 Start/Stop 不再重复设置。

#### Scenario: OpenDecoder initializes and configures the decoder
- **WHEN** 调用 StreamContext::OpenDecoder(video_stream, hw_ctx)
- **THEN** 内部 IDecoder 被初始化且回调已注入，返回 true 表示成功

#### Scenario: OpenDecoder failure does not set callbacks
- **WHEN** decoder_->Open() 返回 false
- **THEN** OpenDecoder 返回 false，不调用 SetFrameCallback/SetEofCallback
