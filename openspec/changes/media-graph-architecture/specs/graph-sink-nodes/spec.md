## ADDED Requirements

### Requirement: VideoSinkNode renders video frames to display
系统 SHALL 定义 `VideoSinkNode`（Sink 类型），继承当前 `VideoRenderer` 的 SDL3 GPU 渲染逻辑，输出到窗口。

VideoSinkNode SHALL 提供：
- 单个输入端口：接收 MediaBuffer（payload 为 MediaFrame，media_type=kVideo）
- 渲染路径：D3D11 零拷贝 → NV12 → YUV420P → swscale fallback
- 同步逻辑：持有 Graph 全局 Clock 引用，在 AudioMaster 模式下按 frame_timer 累积算法决定显示时机
- ThreadingMode：Active（vsync 或时钟驱动循环）

VideoSinkNode SHALL 在渲染每帧后更新 video_clock（VideoMaster 模式）或保持与 MasterClock 同步（AudioMaster 模式）。

#### Scenario: Render a YUV420P software frame
- **WHEN** 输入端口收到 YUV420P 的 MediaFrame
- **THEN** 使用 SDL_UpdateYUVTexture 上传并渲染，画面正确显示

#### Scenario: Render a D3D11 hardware frame zero-copy
- **WHEN** 输入端口收到 pixel_format=D3D11 的 MediaFrame
- **THEN** 从 frame->data[0] 获取 ID3D11Texture2D*，直接绑定为 SDL_Texture 渲染

#### Scenario: Frame sync in AudioMaster mode
- **WHEN** AudioMaster 模式，video_pts=1.06, audio_clock=1.0
- **THEN** frame_timer 累积算法计算 display delay = 40ms（帧间隔），等待后显示

#### Scenario: Keep last frame on video EOF
- **WHEN** 输入端口收到 kEos 标记
- **THEN** 保持最后一帧画面不消失，不退出线程（等待 Stop）

### Requirement: AudioSinkNode plays audio frames through SDL
系统 SHALL 定义 `AudioSinkNode`（Sink 类型），继承当前 `AudioRenderer` 的 SDL 音频输出逻辑。

AudioSinkNode SHALL 提供：
- 单个输入端口：接收 MediaBuffer（payload 为 MediaFrame，media_type=kAudio）
- SDL 音频设备驱动：内部 Pull 数据、resample 到 SDL 格式
- 同步逻辑：持有 Graph 全局 Clock 引用，每消费一帧更新 audio_clock
- 音视频同步：在 AudioMaster 模式下 audio_clock 作为 MasterClock
- ThreadingMode：Active（SDL audio callback 驱动）

#### Scenario: Playback audio frames
- **WHEN** 输入端口收到 S16 44100Hz 立体声的 MediaFrame
- **THEN** 数据 resample 到 SDL 设备格式后输出到扬声器

#### Scenario: Audio clock drives master clock
- **WHEN** AudioMaster 模式，AudioSinkNode 消费完 PTS=5.0 的帧
- **THEN** audio_clock 更新，MasterClock().Get() 返回约 5.0

#### Scenario: Stop on EOF
- **WHEN** 输入端口收到 kEos 标记
- **THEN** 通知 MediaGraph（EventCallback），退出播放循环

### Requirement: FileSinkNode writes data to file
系统 SHALL 定义 `FileSinkNode`（Sink 类型），用于转码场景的最终输出。

FileSinkNode SHALL 提供：
- 单个输入端口：接收 AVPacket 的 MediaBuffer
- Configure 参数：输出文件路径
- Prepare()：打开输出文件
- Process(MediaBuffer)：写入 AVPacket 数据到文件
- ThreadingMode：Active（全速写入）

#### Scenario: Write AVPacket to output file
- **WHEN** 输入端口收到一个 AVPacket，输出文件已打开
- **THEN** AVPacket 数据被写入文件

#### Scenario: Close file on Stop
- **WHEN** 调用 Stop() 或收到 kEos
- **THEN** 输出文件被正确关闭
