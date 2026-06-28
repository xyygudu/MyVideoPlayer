## Purpose

Defines the sink nodes (VideoSinkNode, AudioSinkNode, FileSinkNode) that
consume processed media data at the end of the graph pipeline.

## Requirements

### Requirement: VideoSinkNode renders video frames to display
系统 SHALL 定义 `VideoSinkNode`（Sink 类型），使用 SDL3 GPU 渲染逻辑输出到窗口。

VideoSinkNode SHALL 提供：
- 单个输入端口：接收 MediaBuffer（payload 为 MediaFrame）
- 渲染路径：D3D11 零拷贝 → NV12 → YUV420P → swscale fallback
- 同步逻辑：持有 Clock 引用，在 AudioMaster 模式下按 frame_timer 累积算法决定显示时机
- ThreadingMode：Active

#### Scenario: Render a YUV420P software frame
- **WHEN** 输入端口收到 YUV420P 的 MediaFrame
- **THEN** 使用 SDL_UpdateYUVTexture 上传并渲染

#### Scenario: Frame sync in AudioMaster mode
- **WHEN** AudioMaster 模式，video_pts 与 audio_clock 差值在容差内
- **THEN** frame_timer 累积算法计算 display delay，等待后显示

### Requirement: AudioSinkNode plays audio frames through SDL
系统 SHALL 定义 `AudioSinkNode`（Sink 类型），使用 SDL 音频输出。

AudioSinkNode SHALL 提供：
- 单个输入端口：接收 MediaFrame（media_type=kAudio）
- SDL 音频设备驱动：内部 Pull 数据、resample 到 SDL 格式
- 同步逻辑：每消费一帧更新 audio_clock
- ThreadingMode：Active

#### Scenario: Audio clock drives master clock
- **WHEN** AudioMaster 模式，AudioSinkNode 消费完 PTS=5.0 的帧
- **THEN** audio_clock 更新，MasterClock 返回约 5.0

### Requirement: FileSinkNode writes data to file
系统 SHALL 定义 `FileSinkNode`（Sink 类型），用于转码场景的最终输出。

#### Scenario: Write AVPacket to output file
- **WHEN** 输入端口收到一个 AVPacket，输出文件已打开
- **THEN** AVPacket 数据被写入文件
