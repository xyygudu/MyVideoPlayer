## Requirements

### Requirement: MediaFrame becomes variant payload of MediaBuffer
MediaFrame SHALL 保持其现有接口不变（pts/IsValid/type/RawFrame），但不再作为管线中独立传输的顶层类型，改为 MediaBuffer 的 variant 成员之一（与 AVPacketPtr 并列）。

### Requirement: MediaType enum defines stream categories
系统 SHALL 定义 `enum class MediaType { kUnknown, kAudio, kVideo, kSubtitle }`。

### Requirement: MakeVideoFrame converts MediaFrame to VideoFrame
系统内部 SHALL 提供 `VideoFrame MakeVideoFrame(MediaFrame& mf)` 工厂函数。

### Requirement: MakeAudioFrame converts MediaFrame to AudioFrame
系统内部 SHALL 提供 `AudioFrame MakeAudioFrame(MediaFrame& mf)` 工厂函数。
