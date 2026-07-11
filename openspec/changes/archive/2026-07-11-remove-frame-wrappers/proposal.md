## Why

VideoFrame/AudioFrame 是为"回调给外部渲染"设计的包装类型，但当前架构中渲染已在 core 内完成（SDL3 直渲），回调零订阅。音频路径（AudioSinkNode）从未使用 AudioFrame，直接操作 MediaFrame::RawFrame()。两条路径不对称，且 5 个文件仅服务于视频路径的一次类型转换（MakeVideoFrame → Render），纯属冗余。

## What Changes

- **迁移** PixelFormat / SampleFormat 枚举从 video_frame.h / audio_frame.h 到 media_frame.h
- **迁移** MapPixelFormat 函数从 frame_impl.h 到 video_renderer.cc（匿名 namespace）
- **修改** VideoRenderer::Render 参数 `const VideoFrame&` → `const MediaFrame&`，内部改用 RawFrame() 访问帧数据
- **简化** VideoSinkNode::RenderFrame：去掉 MakeVideoFrame 转换和 frame_cb_ 回调
- **删除** VideoFrameCallback / SetVideoFrameCallback（media_player + video_sink_node）
- **删除** 5 个文件：video_frame.h/cc, audio_frame.h/cc, frame_impl.h

## Capabilities

### New Capabilities
无。本变更为纯清理，不引入新能力。

### Modified Capabilities
- `video-renderer`: Render 接口从 `VideoFrame` 改为 `MediaFrame`（**BREAKING** 对内部调用方，无外部消费者）
- `media-frame`: 迁入 PixelFormat / SampleFormat 枚举定义

## Impact

| 文件 | 影响 |
|------|------|
| `src/media/media_frame.h` | 迁入 PixelFormat / SampleFormat 枚举 |
| `src/media/video_renderer.h/cc` | 接口改为 MediaFrame，迁入 MapPixelFormat |
| `src/media/nodes/video_sink_node.h/cc` | 简化 RenderFrame，移除回调机制 |
| `include/mvp/media_player.h` | 移除 VideoFrameCallback |
| `src/media/media_player.cc` | 移除回调传递 |
| `src/media/graph/media_format.h` | include 更新 |
| `include/mvp/video_frame.h` | **删除** |
| `include/mvp/audio_frame.h` | **删除** |
| `src/media/video_frame.cc` | **删除** |
| `src/media/audio_frame.cc` | **删除** |
| `src/media/frame_impl.h` | **删除** |
