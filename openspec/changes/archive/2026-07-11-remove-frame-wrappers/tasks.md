## 1. 枚举迁移

- [x] 1.1 PixelFormat 枚举从 `include/mvp/video_frame.h` 迁入 `src/media/media_frame.h`（挨着 MediaType）
- [x] 1.2 SampleFormat 枚举从 `include/mvp/audio_frame.h` 迁入 `src/media/media_frame.h`
- [x] 1.3 `src/media/graph/media_format.h` include `mvp/video_frame.h`+`mvp/audio_frame.h` → `media_frame.h`
- [x] 1.4 确认 `decoder_node.cc` 已 include `media_frame.h`（PixelFormat/SampleFormat 引用需可见）

## 2. VideoRenderer 改为接收 MediaFrame

- [x] 2.1 MapPixelFormat 从 `frame_impl.h` 迁入 `video_renderer.cc` 匿名 namespace，补 `#include <libavutil/pixfmt.h>`
- [x] 2.2 `video_renderer.h` include `mvp/video_frame.h` → `media_frame.h`
- [x] 2.3 Render/RenderYUV420P/RenderNV12/RenderHWFrame/RenderFallback 参数 `const VideoFrame&` → `const MediaFrame&`
- [x] 2.4 各 Render 方法内部：帧数据访问改用 `frame.RawFrame()` → `av->width/data[i]/linesize[i]`
- [x] 2.5 `Render()` 分支判断：`frame.format()` → `MapPixelFormat(frame.RawFrame()->format)`

## 3. 简化 VideoSinkNode

- [x] 3.1 `RenderFrame` 简化为 `video_clock_->Set(mf.pts()); renderer_->Render(mf);`
- [x] 3.2 移除 `#include "mvp/video_frame.h"` 和 `#include "frame_impl.h"`
- [x] 3.3 移除 `SetFrameCallback`/`frame_cb_`/`VideoFrameCallback`（video_sink_node.h）

## 4. 清理 MediaPlayer

- [x] 4.1 移除 `VideoFrameCallback`/`SetVideoFrameCallback`/`video_frame_cb_`（media_player.h + cc）
- [x] 4.2 `BuildGraph` 中移除 `vsink->SetFrameCallback(video_frame_cb_)` 调用

## 5. 删除文件

- [x] 5.1 删除 `include/mvp/video_frame.h`、`include/mvp/audio_frame.h`
- [x] 5.2 删除 `src/media/video_frame.cc`、`src/media/audio_frame.cc`、`src/media/frame_impl.h`

## 6. 编译验证

- [x] 6.1 编译通过（`cmake --build build`）
