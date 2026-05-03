## Why

2K 视频播放严重卡顿。性能分析显示 Decoder 线程中 `sws_scale`（CPU 端 YUV→RGB32 转换）占用 26% CPU，叠加软解本身 39%，CPU 被打满。此外视频渲染逻辑耦合在 Qt UI 层（QPainter），core 库无法作为独立音视频库被外部程序使用。需要将像素格式转换卸载到 GPU，并重构渲染管线架构。

## What Changes

- **BREAKING**: `Player::VideoFrameCallback` 签名变更，从 `(const uint8_t*, int, int, int)` 改为 `(const VideoFrame&)`
- 新增 `VideoFrame` class 和 `AudioFrame` class 作为 core 公共 API 的帧抽象，不依赖 FFmpeg
- 新增内部 RAII 包装 `AVFramePtr` / `AVPacketPtr`，消灭所有手动 `av_frame_free`/`av_packet_free`/`av_*_unref`
- Decoder 移除 `sws_scale` RGB 转换，直接输出 YUV420P 帧
- 视频渲染从 Qt UI 层下沉到 core 层，使用 SDL3 GPU 加速渲染（`SDL_UpdateYUVTexture`）
- Qt VideoWidget 退化为嵌入 SDL 窗口的容器，不再参与像素处理

## Capabilities

### New Capabilities
- `frame-abstraction`: 公共 API 层的 VideoFrame/AudioFrame class 封装，以及内部 AVFramePtr/AVPacketPtr RAII 包装
- `video-renderer`: core 层基于 SDL3 GPU 加速的视频渲染模块，接收 YUV 帧并硬件转换显示

### Modified Capabilities
- `demux-decode`: 视频解码器不再做 sws_scale RGB 转换，直接输出原始像素格式帧；PacketQueue/FrameQueue 内部改用 RAII 包装管理生命周期
- `player-ui`: 视频显示不再由 Qt QImage+paintEvent 实现，改为嵌入 SDL3 渲染窗口

## Impact

- **公共 API 破坏性变更**：`VideoFrameCallback` 签名变更，所有外部调用方需适配
- **依赖变化**：core 层新增 SDL3 渲染器使用（SDL3 已是现有依赖，仅音频在用）
- **代码影响**：`decoder.cc`、`decoder.h`、`packet_queue.h`、`frame_queue.h/cc`、`player.cc`、`player.h`、`video_widget.cc/h`、`main_window.cc`
- **移除依赖**：core 层可移除 `libswscale` 链接（如果不再需要任何格式转换）
- **构建影响**：app 层 CMakeLists 移除 Qt OpenGL 相关（不需要），保持 Qt::Widgets 用于控制栏
