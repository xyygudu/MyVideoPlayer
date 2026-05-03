## 1. RAII 基础设施

- [x] 1.1 创建 `src/core/src/ffmpeg_utils.h`，实现 `AVFramePtr` class（alloc/free/move/unref）
- [x] 1.2 在同文件实现 `AVPacketPtr` class（alloc/free/move/unref）
- [x] 1.3 重构 `PacketQueue`：内部 `SerialPacket` 改为持有 `AVPacketPtr`，移除手动 `av_packet_free` 调用
- [x] 1.4 重构 `FrameQueue`：内部改为持有 `AVFramePtr`，移除手动 `av_frame_free` 调用
- [x] 1.5 重构 `Decoder::DecodeLoop`：用 `AVFramePtr`/`AVPacketPtr` 替代裸指针，移除末尾手动 free

## 2. 公共帧抽象

- [x] 2.1 创建 `include/mvp/video_frame.h`，实现 `mvp::VideoFrame` class（move-only，持有 AVBufferRef 引用）
- [x] 2.2 创建 `include/mvp/audio_frame.h`，实现 `mvp::AudioFrame` class（move-only）
- [x] 2.3 创建 `src/core/src/frame_converter.h`，实现内部转换函数 `AVFrame → VideoFrame` 和 `AVFrame → AudioFrame`
- [x] 2.4 更新 `player.h`：`VideoFrameCallback` 签名改为 `void(const VideoFrame&)`

## 3. 去除 CPU 端 YUV→RGB 转换

- [x] 3.1 `Decoder::Start` 移除 `convert_to_rgb` 参数及 `sws_ctx_` 初始化逻辑
- [x] 3.2 `Decoder::EnqueueFrame` 移除 sws_scale 分支，直接 Push 原始帧
- [x] 3.3 `Decoder::DecodeLoop` 移除 `rgb_frame`/`rgb_buffer` 相关代码
- [x] 3.4 `player.cc` 中 `video_ctx_->Start(true)` 改为 `Start()`（移除 bool 参数）
- [x] 3.5 `decoder.h` 移除 `SwsContext*`、`convert_to_rgb_`、`dst_width_/dst_height_` 成员

## 4. SDL3 VideoRenderer

- [x] 4.1 创建 `src/core/src/video_renderer.h`，定义 `VideoRenderer` class 接口（Open/Close/Render/Resize）
- [x] 4.2 创建 `src/core/src/video_renderer.cc`，实现 SDL3 窗口和渲染器创建（嵌入模式，接收 parent window handle）
- [x] 4.3 实现 `VideoRenderer::Render(const VideoFrame&)`：创建/更新 IYUV 纹理 + `SDL_UpdateYUVTexture` + 保持宽高比渲染
- [x] 4.4 实现非 YUV420P 回退路径：检测格式，需要时用 sws_scale 转为 YUV420P 再上传
- [x] 4.5 实现 `VideoRenderer::Resize(int w, int h)` 更新渲染区域

## 5. Player 集成 VideoRenderer

- [x] 5.1 `PlayerImpl` 新增 `VideoRenderer` 成员，在 `Open()` 时初始化
- [x] 5.2 `VideoRenderLoop` 中 Pop 帧后构造 `VideoFrame`，调用 `VideoRenderer::Render()`
- [x] 5.3 保留 `VideoFrameCallback` 作为可选观察者（截帧/录制用途），在 Render 后回调
- [x] 5.4 公共 API 新增 `Player::SetWindowHandle(void* native_handle)` 方法

## 6. UI 层适配

- [x] 6.1 `VideoWidget` 改为纯容器：移除 QOpenGLWidget/QPainter 绘制代码，仅提供 `winId()` 给 core
- [x] 6.2 `MainWindow` 在创建 Player 后调用 `SetWindowHandle(video_widget_->winId())`
- [x] 6.3 `VideoWidget` 实现 `resizeEvent`，通知 Player/VideoRenderer 窗口尺寸变化
- [x] 6.4 移除 `main_window.cc` 中旧的 RGB 回调逻辑

## 7. 构建与清理

- [x] 7.1 `src/core/CMakeLists.txt` 确认 SDL3 链接已包含（现有），评估是否可移除 libswscale（保留用于回退）
- [x] 7.2 `src/app/CMakeLists.txt` 移除 Qt OpenGL 相关依赖（如果有的话）
- [x] 7.3 编译通过，运行 2K 视频验证流畅度
- [x] 7.4 更新 `openspec/specs/demux-decode/spec.md` 和 `openspec/specs/player-ui/spec.md` 归档变更
