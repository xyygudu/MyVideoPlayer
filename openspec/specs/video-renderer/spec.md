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

### Requirement: 渲染器状态仅由渲染线程修改
`VideoRenderer` 的窗口尺寸等渲染状态 SHALL 仅被持有渲染线程的节点在该线程上修改，SHALL NOT 被 UI 线程或其他控制线程直接写入。

理由：这些字段在渲染时被读取。跨线程无同步读写是数据竞争；把修改点收敛到渲染线程即可消除竞态，而无需将每个字段原子化 —— 原子化只是掩盖症状，不改变"谁有权修改"这一职责问题。

#### Scenario: 尺寸变化经由渲染线程落地
- **WHEN** 窗口被缩放
- **THEN** 新尺寸经命令传递到 VideoSinkNode，由其渲染线程调用 `Resize`，UI 线程不触碰 VideoRenderer

#### Scenario: 无需原子成员即无竞态
- **WHEN** 检查 VideoRenderer 的尺寸字段
- **THEN** 它们只有一个写者（渲染线程），普通标量类型即满足线程安全
