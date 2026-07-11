## MODIFIED Requirements

### Requirement: Render 接口使用 MediaFrame
VideoRenderer::Render SHALL 接受 `const MediaFrame&` 参数而非 `const VideoFrame&`。

Render SHALL 通过 `frame.RawFrame()` 获取底层 `AVFrame*` 以访问帧数据（width/height/data/linesize/format）。像素格式分发 SHALL 通过内部 `MapPixelFormat(frame.RawFrame()->format)` 完成。

辅助方法 RenderYUV420P / RenderNV12 / RenderHWFrame / RenderFallback SHALL 同样接受 `const MediaFrame&`。

#### Scenario: YUV420P 帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_YUV420P 的 MediaFrame
- **THEN** RenderYUV420P 被调用，通过 RawFrame()->data/linesize 读取 Y/U/V 平面数据

#### Scenario: 硬件帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_D3D11 的 MediaFrame
- **THEN** RenderHWFrame 被调用，通过 RawFrame() 获取硬件上下文
