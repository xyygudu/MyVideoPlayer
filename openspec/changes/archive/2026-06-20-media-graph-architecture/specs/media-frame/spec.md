## MODIFIED Requirements

### Requirement: MediaFrame becomes variant payload of MediaBuffer
MediaFrame SHALL 保持其现有接口不变（pts/IsValid/type/RawFrame），但不再作为管线中独立传输的顶层类型。SHALL 改为 MediaBuffer 的 variant 成员之一（与 AVPacketPtr 并列）。

MediaFrame 的构造、move 语义、RAII 析构行为 SHALL 完全保持不变。

#### Scenario: MediaFrame as variant alternative
- **WHEN** DecoderNode 输出一帧
- **THEN** 构造 `MediaBuffer(MediaFrame{av_frame, pts, kVideo})`，payload 为 variant 的 MediaFrame 分支

#### Scenario: Existing MediaFrame accessors unchanged
- **WHEN** 通过 `MediaBuffer::AsFrame()` 获取 MediaFrame 引用
- **THEN** pts()、IsValid()、type()、RawFrame() 行为与旧版本完全一致
