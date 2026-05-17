## 1. MediaFrame 底层类型

- [x] 1.1 创建 `src/core/src/media_frame.h`：定义 `MediaType` 枚举和 `MediaFrame` 类（AVFramePtr + pts + MediaType，move-only，访问器 pts/IsValid/type/RawFrame）
- [x] 1.2 创建 `src/core/src/media_frame.cc`：实现 MediaFrame 构造函数（av_frame_ref）、移动语义、访问器
- [x] 1.3 在 `src/core/CMakeLists.txt` 中添加 `media_frame.cc` 源文件
- [x] 1.4 编译验证 MediaFrame 独立编译通过

## 2. IDecoder 接口与 AVFrameDecoder

- [x] 2.1 创建 `src/core/src/i_decoder.h`：定义 `MediaFrameCallback` 类型别名和 `IDecoder` 纯虚接口（Open/Start/Stop/SetDropUntilPts/虚析构）
- [x] 2.2 修改 `src/core/src/decoder.h`：将 `Decoder` 改名为 `AVFrameDecoder`，继承 `IDecoder`；将 `FrameOutputCallback` 替换为 `MediaFrameCallback`；新增 `MediaType media_type_` 成员
- [x] 2.3 修改 `src/core/src/decoder.cc`：`Open()` 中从 `AVStream::codecpar->codec_type` 确定并缓存 `media_type_`；`DecodeLoop` 中构造 `MediaFrame(raw, pts, media_type_)` 并通过 `MediaFrameCallback` 输出（替代原 `AVFrame*` 回调）
- [x] 2.4 编译验证 AVFrameDecoder + IDecoder 编译通过

## 3. StreamContext 重构

- [x] 3.1 重写 `src/core/src/stream_context.h`：从 `template<typename FrameType> struct` 改为 `class StreamContext`；成员改 private（`PacketQueue packet_queue_`、`std::unique_ptr<IDecoder> decoder_`、`FrameQueue<MediaFrame> frame_queue_`）；声明全部 Facade 方法
- [x] 3.2 重写 `src/core/src/stream_context.cc`：消除所有模板特化，实现单一版本的 Start/Stop/Flush/Abort/Reset/FlushAndDropUntil/PopFrame/CurrentSerial/GetPacketQueue/GetFrameQueue/OpenDecoder
- [x] 3.3 删除 stream_context.cc 中的 `MapPixelFormat` 和 `MapSampleFormat`（格式映射将迁移到 frame 工厂函数）
- [x] 3.4 编译验证 StreamContext 独立编译通过

## 4. Frame 边界转换（MakeVideoFrame / MakeAudioFrame）

- [x] 4.1 修改 `src/core/src/frame_impl.h`：添加 `MakeVideoFrame(MediaFrame&)` 和 `MakeAudioFrame(MediaFrame&)` 工厂函数声明；将 `MapPixelFormat`/`MapSampleFormat` 迁入此处（或新建 `frame_factory.h/cc`）
- [x] 4.2 实现 `MakeVideoFrame`：从 MediaFrame::RawFrame() 做 av_frame_ref，执行像素格式映射，构造 VideoFrame
- [x] 4.3 实现 `MakeAudioFrame`：从 MediaFrame::RawFrame() 做 av_frame_ref，执行采样格式映射，构造 AudioFrame
- [x] 4.4 更新 `video_frame.h` / `audio_frame.h` 的 friend 声明以允许工厂函数访问 impl_
- [x] 4.5 编译验证 frame 工厂函数编译通过

## 5. Demuxer API 收敛

- [x] 5.1 修改 `src/core/src/demuxer.h`：新增 `AVStream* AudioStream() const` 和 `AVStream* VideoStream() const`；将 `FormatContext()` 改为 private 或移除
- [x] 5.2 修改 `src/core/src/demuxer.cc`：实现 AudioStream()/VideoStream()（内部用 `format_ctx_->streams[index]`）
- [x] 5.3 编译验证 Demuxer 独立编译通过

## 6. PlayerImpl 适配

- [x] 6.1 修改 `src/core/src/player.cc`：将 `StreamContext<AudioFrame>` / `StreamContext<VideoFrame>` 替换为 `StreamContext`；Open() 中创建 `AVFrameDecoder` 并注入 StreamContext
- [x] 6.2 修改 `player.cc` 中的 `Open()`：使用 `demuxer_.AudioStream()` / `demuxer_.VideoStream()` 替代 `demuxer_.FormatContext()->streams[...]`
- [x] 6.3 修改 `player.cc` 中的 `Seek()`：用 `ctx->SetDropUntilPts(pts)` 替代 `ctx->decoder.SetDropUntilPts(pts)`
- [x] 6.4 修改 `player.cc` 中的 `ResetPipeline()`：用 `ctx->Reset()` 替代 `ctx->packet_queue.Reset()` + `ctx->frame_queue.Reset()`
- [x] 6.5 修改 `player.cc` 中的 `StartPipeline()`：用 `ctx->GetPacketQueue()` / `ctx->GetFrameQueue()` 替代直接成员访问
- [x] 6.6 修改 `player.cc` 中的 `VideoRenderLoop()`：用 `video_ctx_->GetFrameQueue()->Pop()` + `video_ctx_->GetPacketQueue()->serial()`；在渲染前用 `MakeVideoFrame()` 转换 MediaFrame → VideoFrame
- [x] 6.7 编译验证 PlayerImpl 编译通过

## 7. AudioRenderer 适配

- [x] 7.1 修改 `src/core/src/audio_renderer.h`：`Start()` 参数从 `FrameQueue<AudioFrame>*` 改为 `FrameQueue<MediaFrame>*`
- [x] 7.2 修改 `src/core/src/audio_renderer.cc`：直接使用 `MediaFrame::RawFrame()` 操作内部 AVFrame（无需 MakeAudioFrame 中转）
- [x] 7.3 编译验证 AudioRenderer 编译通过

## 8. 构建与集成验证

- [x] 8.1 全量编译通过（cmake --build）
- [ ] 8.2 运行播放器，验证音视频正常播放、A/V 同步正常
- [ ] 8.3 验证 Seek 功能正常（前进/后退）
- [ ] 8.4 验证 Pause/Resume/StepFrame 功能正常
- [ ] 8.5 验证 EOF 到 Finished 状态转换正常
