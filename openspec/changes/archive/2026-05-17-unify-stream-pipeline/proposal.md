## Why

StreamContext 当前是模板 struct（`StreamContext<FrameType>`），成员完全 public，存在两个核心问题：（1）PlayerImpl 在 Seek/Reset/StartPipeline/VideoRenderLoop 中 10 处穿透访问内部成员（`packet_queue`、`frame_queue`、`decoder`），违反迪米特法则；（2）stream_context.cc 中 ~140 行模板特化代码中 Audio/Video 的实现几乎相同，真实差异仅帧类型和格式映射两处，新增流类型需完整复制所有方法。此外，Decoder 是具体类（绑定 FFmpeg send/receive API），无法扩展到字幕等使用不同解码 API 的流类型；Demuxer 暴露 `FormatContext()` 导致 FFmpeg 类型泄漏到调用者。

## What Changes

- 引入 `MediaFrame`（内部统一帧类型）：`AVFramePtr` + `double pts` + `MediaType` 标签，管线内部统一使用，消除 FrameQueue 对具体帧类型的模板依赖
- 引入 `IDecoder` 接口：抽取解码器抽象，现有 `Decoder` 改名 `AVFrameDecoder` 实现该接口，IDecoder 直接输出 `MediaFrame`（由解码器注入 `MediaType`）
- **BREAKING** `StreamContext` 从 `template<typename FrameType> struct` 重构为具体 `class`：成员改 private，补全 Facade 方法集（`FlushAndDropUntil`、`Reset`、`PopFrame`、`CurrentSerial`、`GetPacketQueue`/`GetFrameQueue`），构造时接收 `unique_ptr<IDecoder>`
- `VideoFrame` / `AudioFrame` 公共 API 不变，内部增加从 `MediaFrame` 构造的工厂方法；格式映射（`MapPixelFormat`/`MapSampleFormat`）从 StreamContext 迁移到渲染边界
- `Demuxer` 新增 `AudioStream()` / `VideoStream()` 访问器，移除公开的 `FormatContext()` 方法
- `FrameQueue` 保持模板但管线中统一实例化为 `FrameQueue<MediaFrame>`

## Capabilities

### New Capabilities
- `media-frame`: 内部统一帧类型 MediaFrame 的定义、构造、MediaType 枚举，以及从 MediaFrame 到 VideoFrame/AudioFrame 的边界转换
- `decoder-interface`: IDecoder 抽象接口定义，AVFrameDecoder 实现，回调签名变更为输出 MediaFrame

### Modified Capabilities
- `stream-context`: StreamContext 从模板 struct 重构为具体 class，成员 private 化，补全 Facade 方法集，构造时接收 IDecoder
- `demux-decode`: Demuxer 新增 AudioStream()/VideoStream() 访问器，移除 FormatContext()；PacketQueue/FrameQueue 的模板实例化类型变更
- `frame-abstraction`: VideoFrame/AudioFrame 增加从 MediaFrame 构造的内部工厂方法，格式映射迁入

## Impact

- **核心管线层**（src/core/src/）：stream_context.h/cc 重写，decoder.h/cc 重构为 AVFrameDecoder + IDecoder，新增 media_frame.h/cc 和 i_decoder.h，demuxer.h/cc 接口变更
- **渲染层**：audio_renderer.h/cc 接口从 `FrameQueue<AudioFrame>*` 变为 `FrameQueue<MediaFrame>*`；video_renderer 无接口变更（仍接收 VideoFrame）但 PlayerImpl 的 VideoRenderLoop 在消费侧增加 MediaFrame→VideoFrame 转换
- **公共 API**：Player/VideoFrame/AudioFrame 的公共接口不变，二进制兼容性不受影响
- **编排层**：player.cc 中 `StreamContext<AudioFrame>` / `StreamContext<VideoFrame>` 统一为 `StreamContext`，消除所有穿透访问
- **构建系统**：CMakeLists.txt 需新增 media_frame.cc 源文件
