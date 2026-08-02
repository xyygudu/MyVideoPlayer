## MODIFIED Requirements

### Requirement: VideoSinkNode renders video frames to display
系统 SHALL 定义 `VideoSinkNode`（Sink 类型），使用 SDL3 GPU 渲染逻辑输出到窗口。

VideoSinkNode SHALL 提供：
- 单个输入端口：接收 MediaBuffer（payload 为 MediaFrame）
- 渲染路径：D3D11 零拷贝 → NV12 → YUV420P → swscale fallback
- 同步逻辑：持有 Clock 引用，在 AudioMaster 模式下按 frame_timer 累积算法决定显示时机
- `Negotiate()`：SHALL 校验输入端口已连接且为视频格式，并从 `input_port_->Format().AsVideo().frame_rate` 读取帧率；SHALL NOT 依赖外部 setter 注入帧率
- ThreadingMode：Active

#### Scenario: Render a YUV420P software frame
- **WHEN** 输入端口收到 YUV420P 的 MediaFrame
- **THEN** 使用 SDL_UpdateYUVTexture 上传并渲染

#### Scenario: Frame sync in AudioMaster mode
- **WHEN** AudioMaster 模式，video_pts 与 audio_clock 差值在容差内
- **THEN** frame_timer 累积算法计算 display delay，等待后显示

#### Scenario: Frame rate read from input port format
- **WHEN** VideoSinkNode::Negotiate() 执行且上游已发布含 frame_rate 的 VideoFormat
- **THEN** 节点自身取得帧率用于显示时序计算，无需外部调用 SetVideoFps

#### Scenario: Missing video format fails negotiation
- **WHEN** 输入端口未连接或格式不是视频
- **THEN** Negotiate() 记录 ERROR 并返回 false

### Requirement: AudioSinkNode plays audio frames through SDL
系统 SHALL 定义 `AudioSinkNode`（Sink 类型），使用 SDL 音频输出。

AudioSinkNode SHALL 提供：
- 单个输入端口：接收 MediaFrame（media_type=kAudio）
- SDL 音频设备驱动：内部 Pull 数据、resample 到 SDL 格式
- 同步逻辑：每消费一帧更新 audio_clock
- `Negotiate()`：SHALL 从输入端口格式读取 sample_rate / channels（格式推理属协商期职责）
- `Prepare()`：SHALL 仅打开 SDL 音频设备，不再做格式推理
- ThreadingMode：Active

#### Scenario: Audio clock drives master clock
- **WHEN** AudioMaster 模式，AudioSinkNode 消费完 PTS=5.0 的帧
- **THEN** audio_clock 更新，MasterClock 返回约 5.0

#### Scenario: Audio params resolved during negotiation
- **WHEN** AudioSinkNode::Negotiate() 执行
- **THEN** sample_rate 与 channels 从输入端口格式解析完成，Prepare() 直接用其打开 SDL 设备

#### Scenario: Missing audio params fails negotiation
- **WHEN** 输入端口格式既非 AudioFormat 也不含可用的 codec_params
- **THEN** Negotiate() 记录 ERROR 并返回 false
