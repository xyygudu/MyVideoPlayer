## Context

当前播放器管线：`Demuxer → PacketQueue → Decoder(sws_scale YUV→RGB32) → FrameQueue → Player::VideoRenderLoop → callback → Qt QImage+paintEvent`。

问题：
1. sws_scale 在 CPU 上做 2K YUV→RGB 转换占用 26% CPU，是主要性能瓶颈
2. 视频渲染逻辑在 Qt UI 层实现，core 库不可独立使用
3. AVFrame/AVPacket 裸指针手动管理 free/unref，代码脆弱易泄漏

参考实现：FFplay（SDL YUV texture 直渲）、MPV（mp_image RAII + GPU vo）、VLC（picture_t + vout）、QtAV（C++ RAII 封装）。

## Goals / Non-Goals

**Goals:**
- 消除 CPU 端 YUV→RGB 转换，2K 视频流畅播放
- core 层拥有完整的视频渲染能力（SDL3 GPU），可脱离 Qt 独立使用
- 公共 API 暴露 VideoFrame/AudioFrame class，不泄漏 FFmpeg 类型
- 内部用 RAII 管理 AVFrame/AVPacket 生命周期，消灭手动 free/unref

**Non-Goals:**
- 硬件解码（DXVA2/VAAPI）— 留作后续优化
- 音频重采样重构 — 本次不涉及音频渲染改动
- 字幕渲染
- 支持非 YUV420P 输出格式的自动转换（如 YUV422P/10bit）— 后续按需添加

## Decisions

### 1. 视频渲染方案：SDL3 YUV Texture

**选择**：SDL3 `SDL_CreateTexture(SDL_PIXELFORMAT_IYUV)` + `SDL_UpdateYUVTexture()`

**替代方案**：
- QOpenGLWidget + GLSL shader：需要自己管理 GL context，且渲染逻辑留在 UI 层
- D3D11 直接调用：平台相关，不跨平台

**理由**：
- SDL3 已是项目依赖（音频在用），零新增依赖
- SDL3 在 Windows 默认使用 D3D11 后端，GPU 硬件做 YUV→RGB
- 对齐 FFplay 做法（SDL 就是 FFplay 的渲染后端）
- core 层自包含，UI 层只需嵌入 SDL window 或完全不用

### 2. 帧抽象方案：分离 VideoFrame / AudioFrame class

**选择**：独立的 `class VideoFrame` 和 `class AudioFrame`，move-only RAII 语义

**替代方案**：
- 统一 Frame struct：音视频字段差异大（planes vs samples），浪费空间且类型不安全
- 直接暴露 AVFrame*：耦合 FFmpeg，外部无法独立使用

**理由**：
- 对齐 MPV (`mp_image` / `mp_aframe`)、VLC (`picture_t` / `block_t`)
- class 而非 struct：支持析构释放、方法扩展（如未来的 `ConvertTo()`）、禁止拷贝
- 内部持有引用计数缓冲区（复用 AVFrame 的 AVBufferRef），出队时 ref+1 后 AVFrame 可立即 unref

### 3. 内部 RAII：AVFramePtr / AVPacketPtr

**选择**：薄包装 class，`unique_ptr`-like 语义，析构自动 `av_frame_free`/`av_packet_free`

**替代方案**：
- `unique_ptr<AVFrame, custom_deleter>`：可行但语义不够清晰，无法附加 `unref()` 等便捷方法
- 不包装：现状，手动管理易出错

**理由**：
- 对齐 C++ 音视频项目通行做法（QtAV、libopenshot）
- 析构保证不泄漏，move 语义保证所有权清晰
- PacketQueue 的 `SerialPacket` 改为持有 `AVPacketPtr`，Flush 时自动释放

### 4. 渲染架构分层

**选择**：core 新增 `VideoRenderer` 类，拥有 SDL_Window + SDL_Renderer + SDL_Texture

```
core 层: Decoder → FrameQueue → VideoRenderer(SDL3 GPU)
app 层: Qt MainWindow 嵌入 SDL window（通过 native window handle）
```

**替代方案**：
- 继续用 callback 让外部渲染：core 不自包含，视频渲染能力依赖调用方
- core 提供 callback + 内置 renderer 双模式：过度设计

**理由**：
- 保持 VideoFrameCallback 作为可选观察者（允许外部截帧/录制），但渲染由 core 内部完成
- SDL window 可嵌入任意 native 窗口（Qt、Win32、无头渲染），灵活性不减

## Risks / Trade-offs

- **[SDL 窗口嵌入 Qt]** Qt 嵌入外部 native window 在某些平台有 focus/resize 问题 → 通过 `SDL_CreateWindowFrom()` (SDL3: `SDL_CreateWindowWithProperties` + parent handle) 嵌入，已有成熟方案
- **[非 YUV420P 视频]** 部分视频解码出 YUV422P/NV12/10bit 格式，直接传入 SDL IYUV texture 会失败 → 第一期只处理 YUV420P，非 420P 格式保留 sws_scale 回退路径（延迟转换仅在需要时触发）
- **[公共 API 破坏]** VideoFrameCallback 签名变更 → 当前只有 app 层一个调用方，影响可控
- **[RAII 引入的 move 语义]** 队列操作需要适配 move → 改动范围集中在 PacketQueue/FrameQueue 内部
