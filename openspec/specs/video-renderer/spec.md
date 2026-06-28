## Requirements

### Requirement: VideoRenderer becomes VideoSinkNode
VideoRenderer 的核心渲染逻辑（SDL3 GPU 后端、D3D11 零拷贝、NV12 上传、YUV420P 上传、swscale fallback）SHALL 保留，但封装方式从独立类变更为实现 INode 接口的 `VideoSinkNode`。

VideoSinkNode SHALL 增加同步职责（frame_timer 累积计算、video_clock 更新），这些逻辑原属于 PlayerImpl。

渲染路径选择逻辑（按帧格式分支）：
1. `AV_PIX_FMT_D3D11` → 零拷贝 D3D11 texture 直通
2. `AV_PIX_FMT_NV12`（CPU）→ SDL_UpdateNV12Texture
3. `YUV420P` → SDL_UpdateYUVTexture
4. 其他格式 → sws_scale 转 YUV420P 后上传（fallback）

#### Scenario: All render paths preserved
- **WHEN** VideoSinkNode 收到 YUV420P / NV12 / D3D11 / 其他格式的帧
- **THEN** 渲染行为与旧 VideoRenderer 完全一致

#### Scenario: Sink node receives frame from Link
- **WHEN** 上游 DecoderNode Push 帧到 Link
- **THEN** VideoSinkNode 从 InputPort::Pull() 获取帧

### Requirement: AudioRenderer becomes AudioSinkNode
AudioRenderer 的核心音频输出逻辑（SDL 音频设备、resample 到 SDL 格式）SHALL 保留，封装为 `AudioSinkNode`。

#### Scenario: Audio playback behavior preserved
- **WHEN** AudioSinkNode 收到音频 MediaFrame
- **THEN** 数据处理和播放行为与旧 AudioRenderer 完全一致
