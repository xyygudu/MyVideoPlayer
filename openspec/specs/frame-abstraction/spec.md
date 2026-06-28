## Requirements

### Requirement: VideoFrame and AudioFrame remain public API types
VideoFrame 和 AudioFrame SHALL 保持其现有接口完全不变。内部构造路径 SHALL 保持 MakeVideoFrame/MakeAudioFrame 工厂函数，调用位置从 FrameQueue 消费端变为 SinkNode 中。

### Requirement: FrameQueue de-templated into Link
FrameQueue 模板类的功能 SHALL 被 Link 模板替代。FrameQueue 的 QueueEntry 概念提升到 MediaBuffer 层面（payload + serial + flags）。

### Requirement: Format mapping resides at render boundary
像素格式映射和采样格式映射 SHALL 在 MakeVideoFrame/MakeAudioFrame 工厂函数中执行，不在管线传输层中执行。
