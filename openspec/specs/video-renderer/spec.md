## ADDED Requirements

### Requirement: VideoRenderer renders YUV frames via SDL3 GPU
系统 SHALL 在 core 层提供 `VideoRenderer` 类，使用 SDL3 的 GPU 加速后端渲染视频帧。VideoRenderer SHALL 创建 `SDL_Texture`（格式为 `SDL_PIXELFORMAT_IYUV`），通过 `SDL_UpdateYUVTexture()` 上传 YUV420P 数据，由 GPU 硬件完成 YUV→RGB 转换和显示。

#### Scenario: Render a YUV420P frame
- **WHEN** VideoRenderer 接收到一帧 YUV420P 的 VideoFrame
- **THEN** 帧数据上传至 GPU 纹理并渲染到窗口，画面正确显示

#### Scenario: Resolution change during playback
- **WHEN** 视频帧分辨率发生变化（如切换文件）
- **THEN** VideoRenderer 重新创建匹配新分辨率的纹理，继续正常渲染

#### Scenario: Maintain aspect ratio
- **WHEN** 渲染窗口尺寸与视频宽高比不一致
- **THEN** 视频画面保持原始宽高比居中显示，空余区域填黑

### Requirement: VideoRenderer supports embedded window mode
VideoRenderer SHALL 支持将 SDL 渲染窗口嵌入外部提供的 native window handle（如 Qt 的 `winId()`）。外部程序通过传入 parent window handle 创建 VideoRenderer，SDL 窗口作为子窗口存在。

#### Scenario: Embed in Qt widget
- **WHEN** 外部传入 Qt widget 的 native window handle 创建 VideoRenderer
- **THEN** SDL 渲染窗口作为子窗口嵌入该 widget 区域内

#### Scenario: Resize follows parent
- **WHEN** 父窗口（Qt widget）尺寸变化
- **THEN** VideoRenderer 跟随调整渲染区域大小

### Requirement: VideoRenderer lifecycle management
VideoRenderer SHALL 支持 Open/Close 生命周期。Open 时创建 SDL 窗口和渲染器资源，Close 时释放所有 SDL 资源。重复 Open 前 SHALL 先 Close。

#### Scenario: Open creates rendering resources
- **WHEN** 调用 VideoRenderer::Open 并传入窗口参数
- **THEN** 创建 SDL_Window、SDL_Renderer，准备就绪接收帧

#### Scenario: Close releases resources
- **WHEN** 调用 VideoRenderer::Close
- **THEN** 销毁 SDL_Texture、SDL_Renderer、SDL_Window，释放 GPU 资源

### Requirement: Non-YUV420P fallback path
当解码输出格式不是 YUV420P 时，VideoRenderer SHALL 使用 `sws_scale` 将帧转换为 YUV420P 后再上传 GPU。此转换路径仅作为兼容回退，非主路径。

#### Scenario: Render a non-YUV420P frame
- **WHEN** VideoRenderer 接收到 YUV422P 或 NV12 格式的帧
- **THEN** 内部使用 sws_scale 转为 YUV420P 后正常渲染，不崩溃
