## Requirements

### Requirement: VideoFrame and AudioFrame remain public API types
VideoFrame 和 AudioFrame SHALL 保持其现有接口完全不变。内部构造路径 SHALL 保持 MakeVideoFrame/MakeAudioFrame 工厂函数，调用位置从 FrameQueue 消费端变为 SinkNode 中。

### Requirement: FrameQueue de-templated into Link
FrameQueue 模板类的功能 SHALL 被 Link 模板替代。FrameQueue 的 QueueEntry 概念提升到 MediaBuffer 层面（payload + serial + flags）。

### Requirement: Format mapping resides at render boundary
像素格式映射和采样格式映射 SHALL 在 MakeVideoFrame/MakeAudioFrame 工厂函数中执行，不在管线传输层中执行。

### Requirement: MediaFrame 提供平面数据访问方法
`MediaFrame` SHALL 新增 `width()`/`height()`/`format()`/`PlaneData(int)`/`PlaneLinesize(int)` 方法，封装 AVFrame 内部平面布局访问，使调用方无需通过 `RawFrame()` 获取裸指针即可读写像素数据。

#### Scenario: 通过 PlaneData 访问 Y 平面
- **WHEN** 调用 `frame.PlaneData(0)` 和 `frame.PlaneLinesize(0)`
- **THEN** 返回值与 `av_frame` 内部 `data[0]`/`linesize[0]` 一致

### Requirement: MediaFrame 提供 MakeWritable 可写性保障
`MediaFrame` SHALL 提供 `[[nodiscard]] MediaFrame MakeWritable() const`，内部调用 `av_frame_is_writable`/`av_frame_make_writable` 确保返回值可安全写入。调用方通过返回值操作可写帧。

#### Scenario: 共享帧触发深拷贝
- **WHEN** 某 MediaFrame 被多处引用，调用 `MakeWritable()`
- **THEN** 返回的帧拥有独立的内存副本

### Requirement: MediaFrame 提供 CreateSameFormat 工厂方法
`MediaFrame` SHALL 提供 `static MediaFrame CreateSameFormat(const MediaFrame& ref, double pts)`，返回与 ref 同尺寸/格式的空帧（零初始化，不拷贝像素数据）。
