## ADDED Requirements

### Requirement: MediaFrame encapsulates decoded frame for pipeline transport
系统内部 SHALL 提供 `MediaFrame` 类，作为管线内部统一的帧传输类型。MediaFrame SHALL 为 move-only 语义（禁止拷贝）。MediaFrame SHALL 不暴露在公共 API 头文件中（仅限 core/src 内部使用）。

MediaFrame SHALL 持有以下数据：
- `AVFramePtr frame_`：RAII 管理的 AVFrame（引用计数）
- `double pts_`：显示时间戳（秒）
- `MediaType type_`：媒体类型标签

MediaFrame SHALL 暴露以下只读访问器：
- `double pts() const`：返回时间戳
- `bool IsValid() const`：返回内部 AVFrame 是否有效（非 null 且有数据）
- `MediaType type() const`：返回媒体类型
- `AVFrame* RawFrame() const`：返回内部 AVFrame 原始指针（仅限内部模块使用）

MediaFrame 析构时 SHALL 自动释放其持有的 AVFrame 引用。

#### Scenario: Construct MediaFrame from raw AVFrame
- **WHEN** Decoder 解码出一帧并构造 MediaFrame（传入 AVFrame*、pts、MediaType）
- **THEN** MediaFrame 持有该帧的引用（av_frame_ref），pts 和 type 被正确设置

#### Scenario: MediaFrame move semantics
- **WHEN** MediaFrame 被 move 到另一个 MediaFrame 对象
- **THEN** 源对象变为无效（IsValid() 返回 false），目标对象持有数据

#### Scenario: MediaFrame destruction releases buffer
- **WHEN** MediaFrame 对象析构
- **THEN** 内部 AVFrame 引用计数减一

#### Scenario: Default-constructed MediaFrame is invalid
- **WHEN** 使用默认构造函数创建 MediaFrame
- **THEN** IsValid() 返回 false，pts() 返回 0.0，type() 返回 MediaType::kUnknown

### Requirement: MediaType enum defines stream categories
系统 SHALL 定义 `enum class MediaType`，包含以下值：
- `kUnknown`：未知/无效
- `kAudio`：音频流
- `kVideo`：视频流
- `kSubtitle`：字幕流（预留）

#### Scenario: MediaType distinguishes stream types
- **WHEN** 系统创建音频管线的 MediaFrame
- **THEN** MediaFrame::type() 返回 MediaType::kAudio

#### Scenario: MediaType supports subtitle extension
- **WHEN** 未来需要字幕流支持
- **THEN** 可直接使用 MediaType::kSubtitle，无需修改现有枚举

### Requirement: MakeVideoFrame converts MediaFrame to VideoFrame
系统内部 SHALL 提供 `VideoFrame MakeVideoFrame(MediaFrame& mf)` 工厂函数（友元，不暴露在公共 API）。该函数 SHALL：
1. 从 `mf.RawFrame()` 通过 `av_frame_ref` 获取帧引用
2. 执行像素格式映射（`AVFrame::format` → `PixelFormat` 枚举）
3. 构造并返回有效的 VideoFrame

#### Scenario: Successful video frame conversion
- **WHEN** 对一个 type==kVideo 的 MediaFrame 调用 MakeVideoFrame
- **THEN** 返回有效的 VideoFrame，format/width/height/pts 均正确映射

#### Scenario: Format mapping covers common pixel formats
- **WHEN** MediaFrame 内部 AVFrame 的 format 为 AV_PIX_FMT_YUV420P/NV12/D3D11 等
- **THEN** MakeVideoFrame 返回的 VideoFrame::format() 对应 PixelFormat::kYUV420P/kNV12/kD3D11

### Requirement: MakeAudioFrame converts MediaFrame to AudioFrame
系统内部 SHALL 提供 `AudioFrame MakeAudioFrame(MediaFrame& mf)` 工厂函数（友元，不暴露在公共 API）。该函数 SHALL：
1. 从 `mf.RawFrame()` 通过 `av_frame_ref` 获取帧引用
2. 执行采样格式映射（`AVFrame::format` → `SampleFormat` 枚举）
3. 构造并返回有效的 AudioFrame

#### Scenario: Successful audio frame conversion
- **WHEN** 对一个 type==kAudio 的 MediaFrame 调用 MakeAudioFrame
- **THEN** 返回有效的 AudioFrame，format/nb_samples/channels/sample_rate/pts 均正确

#### Scenario: Format mapping covers common sample formats
- **WHEN** MediaFrame 内部 AVFrame 的 format 为 AV_SAMPLE_FMT_S16/FLT/FLTP 等
- **THEN** MakeAudioFrame 返回的 AudioFrame::format() 对应 SampleFormat::kS16/kFloat/kFloatPlanar

### Requirement: FrameQueue instantiated with MediaFrame in pipeline
管线中 FrameQueue SHALL 实例化为 `FrameQueue<MediaFrame>`。QueueEntry 模板 SHALL 适配 MediaFrame 类型：

```
template<>
struct QueueEntry<MediaFrame> {
    MediaFrame frame;
    int serial;
    bool eof{false};
};
```

#### Scenario: MediaFrame flows through FrameQueue
- **WHEN** Decoder 产出 MediaFrame 并 Push 到 FrameQueue<MediaFrame>
- **THEN** 消费者可通过 Pop 获取完整的 MediaFrame（包含 type、pts、RawFrame）

#### Scenario: EOF marker in MediaFrame queue
- **WHEN** 调用 PushEof(serial)
- **THEN** Pop 返回的 QueueEntry 中 eof==true，frame 为默认构造的无效 MediaFrame
