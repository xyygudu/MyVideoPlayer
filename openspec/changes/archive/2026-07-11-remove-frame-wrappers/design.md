## Context

当前帧数据流中有两层抽象：
- `MediaFrame`：内部帧类型，持有 `AVFrame*`（RawFrame），在 Pipeline Link 中流转
- `VideoFrame` / `AudioFrame`：对外包装类型，原设计用于回调给外部消费

实际使用中：
- `AudioSinkNode` 直接用 `MediaFrame::RawFrame()`，从未使用 `AudioFrame`
- `VideoFrame` 的唯一外部消费者 `MediaPlayer::VideoFrameCallback` 零订阅
- `VideoFrame` 仅剩的消费者是内部 `VideoRenderer`，包装仅为一次 `MakeVideoFrame` → `Render(VideoFrame)` 的类型转换

## Goals / Non-Goals

**Goals:**
- 统一 Video/Audio 路径使用 MediaFrame，消除不对称
- 删除 VideoFrame/AudioFrame/FrameImpl 5 个冗余文件
- PixelFormat/SampleFormat 枚举保留（media_format.h 依赖），迁入 media_frame.h

**Non-Goals:**
- 不改变 MediaFrame 的接口或实现
- 不改变 VideoRenderer 的渲染逻辑（仅改参数类型）
- 不影响 AudioSinkNode（已是直接操作 RawFrame）

## Decisions

### 1. 枚举归宿：media_frame.h

`PixelFormat` 和 `SampleFormat` 由 `media_format.h` 的 `VideoFormat`/`AudioFormat`/`FormatCaps` 使用，也由 `decoder_node.cc` 的 `Negotiate()` 使用。放在 `media_frame.h`（挨着 `MediaType`）是最小依赖路径——所有现有消费者已包含 `media_frame.h`。

### 2. MapPixelFormat 迁入 video_renderer.cc

`MapPixelFormat(int av_pix_fmt)` 的唯一调用方是 `MakeVideoFrame`（在 frame_impl.h），而 `MakeVideoFrame` 的唯一调用方是 `VideoSinkNode::RenderFrame`。删除 `MakeVideoFrame` 后，映射逻辑需要在 `VideoRenderer::Render` 内部完成。放入 `video_renderer.cc` 匿名 namespace 是最小作用域。

### 3. VideoRenderer 接口改为 MediaFrame

Render 及四个 helper 参数从 `const VideoFrame&` 改为 `const MediaFrame&`。内部帧数据访问：
- `frame.format()` → `MapPixelFormat(frame.RawFrame()->format)`
- `frame.width()` → `frame.RawFrame()->width`
- `frame.data(i)` → `frame.RawFrame()->data[i]`
- `frame.linesize(i)` → `frame.RawFrame()->linesize[i]`

### 4. 移除 VideoFrameCallback

回调零订阅，且 app 通过 `SetWindowHandle + SDL3` 直接渲染。移除整个回调链路：媒体层 → MediaPlayer → VideoSinkNode。

## Risks / Trade-offs

- [风险] `MediaPlayer::SetVideoFrameCallback` 是公共 API，移除是 **BREAKING** → 缓解：零外部消费，无实际影响
- [风险] `PixelFormat` 枚举值依赖 `AV_PIX_FMT_*` 宏（在 MapPixelFormat 中映射）→ 缓解：枚举定义本身不依赖 FFmpeg，映射在 video_renderer.cc 中完成，分离清晰
