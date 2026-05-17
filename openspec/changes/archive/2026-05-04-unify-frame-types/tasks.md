## 1. VideoFrame/AudioFrame Impl 改用 AVFramePtr

- [x] 1.1 修改 `frame_impl.h`：VideoFrame::Impl 和 AudioFrame::Impl 的 `AVFrame*` 成员改为 `AVFramePtr`，删除手写析构函数
- [x] 1.2 修改 `video_frame.cc` / `audio_frame.cc`：访问器中 `impl_->frame` 改为 `impl_->frame.get()` 访问原始指针
- [x] 1.3 编译验证通过

## 2. 引入 DecoderParams 值类型

- [x] 2.1 在 `decoder.h` 中定义 `struct DecoderParams { AVRational time_base; AVRational frame_rate; }`
- [x] 2.2 修改 `Decoder::OpenCodec` 接口：接收 `DecoderParams` 参数并存储为成员 `params_`
- [x] 2.3 修改 `StreamContext::OpenDecoder`：从 AVStream 萃取 time_base/frame_rate 构建 DecoderParams 传给 Decoder
- [x] 2.4 编译验证通过

## 3. FrameQueue 模板化

- [x] 3.1 定义 `QueueEntry<T>` 模板结构体（替代 SerialFrame）
- [x] 3.2 将 `FrameQueue` 改为 `FrameQueue<T>` 类模板，Push/Pop 使用 `QueueEntry<T>`
- [x] 3.3 在 `frame_queue.cc` 中添加显式实例化：`FrameQueue<VideoFrame>` 和 `FrameQueue<AudioFrame>`
- [x] 3.4 删除 `SerialFrame` 结构体定义
- [x] 3.5 编译验证通过

## 4. StreamContext 模板化

- [x] 4.1 将 `StreamContext` 改为 `StreamContext<FrameType>` 类模板，持有 `FrameQueue<FrameType>`
- [x] 4.2 在 `stream_context.cc` 中添加显式特化（VideoFrame/AudioFrame）
- [x] 4.3 修改 `player.cc`：`video_ctx_` 类型改为 `StreamContext<VideoFrame>`，`audio_ctx_` 改为 `StreamContext<AudioFrame>`
- [x] 4.4 编译验证通过

## 5. Decoder 通过回调输出公共帧类型

- [x] 5.1 Decoder 改用 `FrameOutputCallback` 回调输出帧（PTS 换算在 DrainFrames 中完成）
- [x] 5.2 StreamContext::Start() 创建 lambda 完成 av_frame_ref + 格式映射 + 构建 VideoFrame/AudioFrame 并 Push 到 FrameQueue
- [x] 5.3 编译验证通过

## 6. VideoRenderLoop 去除 FFmpeg 依赖

- [x] 6.1 移除 VideoRenderLoop 中的 `AVStream* video_stream` 局部变量
- [x] 6.2 PTS 改为直接 `entry->frame.pts()`，删除手动换算代码
- [x] 6.3 移除 `FrameConverter::ToVideoFrame` 调用，直接传递 `entry->frame` 给渲染器和回调
- [x] 6.4 移除 player.cc 中的 `#include <libavutil/frame.h>` 和 `#include "frame_converter.h"`
- [x] 6.5 编译验证通过

## 7. AudioRenderer 适配

- [x] 7.1 修改 AudioRenderer::Start 参数：接收 `FrameQueue<AudioFrame>*` 替代 `FrameQueue*`
- [x] 7.2 AudioLoop 中通过 `af.impl_->frame.get()` 获取 AVFrame* 供 swr_convert 使用（planar 格式需要）
- [x] 7.3 缓存 sample_rate_/channels_ 替代运行时访问 AVStream*
- [x] 7.4 编译验证通过

## 8. 清理废弃代码

- [x] 8.1 删除 `frame_converter.h` 和 `frame_converter.cc`
- [x] 8.2 CMakeLists 使用 GLOB_RECURSE，无需手动移除
- [x] 8.3 所有 include 引用已在前置任务中清除
- [x] 8.4 全量编译验证通过
