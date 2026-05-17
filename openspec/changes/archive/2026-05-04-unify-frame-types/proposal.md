## Why

当前帧管线存在三层类型（`AVFramePtr` → `SerialFrame` → `VideoFrame/AudioFrame`），导致 PTS 计算散落在 VideoRenderLoop、FrameConverter 等处，显示层仍直接接触 FFmpeg 类型（`AVFrame*`、`AVStream*`）。这违反了 pimpl 封装的初衷，增加了认知负担和改动风险。

## What Changes

- **BREAKING** `FrameQueue` 模板化为 `FrameQueue<T>`，传输单元改为 `QueueEntry<T> { T frame; int serial; bool eof; }`
- **BREAKING** `StreamContext` 模板化为 `StreamContext<FrameType>`，视频/音频各自持有对应类型的 FrameQueue
- Decoder 接收值类型 `DecoderParams { time_base, frame_rate }` 代替运行时使用 `AVStream*`，解码后直接输出 `VideoFrame`/`AudioFrame`（PTS 已换算为秒）
- `VideoFrame::Impl` 内部改用 `AVFramePtr` 替代裸 `AVFrame*`
- `FrameConverter` 类废弃，其逻辑内联到 Decoder
- `VideoRenderLoop` 不再接触 `AVFrame*`/`AVStream*`，直接从 `QueueEntry<VideoFrame>` 取 `frame.pts()`
- `AudioRenderer` 改为从 `FrameQueue<AudioFrame>` 消费，通过 `AudioFrame` 访问器获取数据

## Capabilities

### New Capabilities

（无新 capability——所有改动均为现有 capability 的实现重构）

### Modified Capabilities

- `demux-decode`: FrameQueue 传输单元从 `SerialFrame { AVFramePtr }` 改为 `QueueEntry<VideoFrame/AudioFrame>`；Decoder 输出类型从 AVFramePtr 改为 VideoFrame/AudioFrame
- `frame-abstraction`: VideoFrame/AudioFrame 由 Decoder 直接构建（而非显示层延迟创建）；Impl 改用 AVFramePtr
- `stream-context`: StreamContext 模板化，持有 `FrameQueue<VideoFrame>` 或 `FrameQueue<AudioFrame>`

## Impact

- **源文件**：frame_queue.h/cc、decoder.h/cc、stream_context.h/cc、player.cc、audio_renderer.cc、frame_impl.h、video_frame.cc、audio_frame.cc
- **废弃文件**：frame_converter.h/cc（逻辑迁移到 decoder）
- **公共 API**：VideoFrame/AudioFrame 接口不变（内部实现变化对外透明）
- **编译影响**：FrameQueue 模板化需要显式实例化或 header-only；选择显式实例化保持编译隔离
