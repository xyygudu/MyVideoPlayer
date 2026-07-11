## Purpose

Defines the VideoRenderer's rendering interface, which consumes MediaFrame
directly (no intermediate VideoFrame wrapper).

## Requirements

### Requirement: Render 接口使用 MediaFrame
VideoRenderer::Render SHALL 接受 `const MediaFrame&` 参数。

Render SHALL 通过 `frame.RawFrame()` 获取底层 `AVFrame*` 以访问帧数据。像素格式分发 SHALL 通过内部 `MapPixelFormat(frame.RawFrame()->format)` 完成。

渲染路径选择（按帧格式分支）：
1. `AV_PIX_FMT_D3D11` → 零拷贝 D3D11 texture 直通
2. `AV_PIX_FMT_NV12` → SDL_UpdateNVTexture
3. `AV_PIX_FMT_YUV420P` → SDL_UpdateYUVTexture
4. 其他格式 → sws_scale 转 YUV420P 后上传

#### Scenario: YUV420P 帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_YUV420P 的 MediaFrame
- **THEN** RenderYUV420P 被调用，通过 RawFrame()->data/linesize 读取数据

#### Scenario: 硬件帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_D3D11 的 MediaFrame
- **THEN** RenderHWFrame 被调用，通过 RawFrame() 获取硬件上下文
