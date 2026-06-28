## MODIFIED Requirements

### Requirement: VideoFrame and AudioFrame remain public API types
VideoFrame 和 AudioFrame SHALL 保持其现有接口完全不变（move-only 语义、Pimpl 隐藏 FFmpeg 类型、data/linesize/width/height/format/pts 等访问器）。

内部构造路径 SHALL 保持 `MakeVideoFrame(MediaFrame&)` / `MakeAudioFrame(MediaFrame&)` 工厂函数，但调用位置从"FrameQueue 消费端"变为"SinkNode 的 Process() 或渲染回调中"。

#### Scenario: VideoFrame created from MediaBuffer in SinkNode
- **WHEN** VideoSinkNode 从 InputPort 获取 MediaBuffer，通过 AsFrame() 获取 MediaFrame，调用 MakeVideoFrame
- **THEN** VideoFrame 行为与旧版本完全一致

#### Scenario: Public API unchanged
- **WHEN** App 层使用 VideoFrame/AudioFrame 的访问器
- **THEN** 编译和运行行为与旧版本完全一致

### Requirement: FrameQueue de-templated into Link
FrameQueue<T> 模板类的功能 SHALL 被 Link 模板替代。FrameQueue 的 QueueEntry 概念（frame + serial + eof）SHALL 提升到 MediaBuffer 层面（payload + serial + flags）。

旧 FrameQueue 的三种实例化（`FrameQueue<MediaFrame>`, `<VideoFrame>`, `<AudioFrame>`）SHALL 全部删除，仅保留 `Link<CountCapacity>` 承载 MediaBuffer。

#### Scenario: Frame transport through Link instead of FrameQueue
- **WHEN** DecoderNode Push MediaBuffer 到 Link
- **THEN** Link 的 push/pop 行为与旧 FrameQueue 一致（阻塞、容量限制、serial 验证）
