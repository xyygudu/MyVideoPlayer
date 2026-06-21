## ADDED Requirements

### Requirement: AVFilterNode wraps FFmpeg libavfilter graph
系统 SHALL 定义 `AVFilterNode`（Transform 类型），包装 FFmpeg 的 `AVFilterGraph` + `buffersrc`/`buffersink` 对。

AVFilterNode SHALL 提供：
- 单个输入端口 + 单个输出端口（初版）
- Configure 参数：滤镜描述字符串（如 `"scale=1280:720,eq=brightness=0.1"`）或预构建的 AVFilterGraph
- Negotiate()：根据输入 MediaFormat 配置 buffersrc，从 buffersink 读取输出格式
- Process(MediaBuffer, OutputCallback emit)：调用 `av_buffersrc_add_frame()` → 循环 `av_buffersink_get_frame()` → 每帧调用 `emit(buf)`
- ThreadingMode：Passive（默认）

注意：某些滤镜（如 framerate、interpolate）可能从 1 帧产出 0 或多帧，因此使用 OutputCallback 而非返回值。

#### Scenario: Apply scale filter on video frame
- **WHEN** AVFilterNode 配置 "scale=1280:720"，输入 1920×1080 的帧
- **THEN** Process() 输出 1280×720 的帧，pixel_format 保持不变

#### Scenario: Apply multiple chained filters
- **WHEN** AVFilterNode 配置 "scale=1280:720,eq=brightness=0.1"
- **THEN** 帧先缩放后调整亮度，最终输出正确

#### Scenario: Filter runtime parameter update
- **WHEN** 通过 Reconfigure("eq=gamma=1.5") 更新滤镜参数
- **THEN** 后续帧按新参数处理，无需重启节点

#### Scenario: Process on upstream thread (Passive mode)
- **WHEN** Passive AVFilterNode 的 Process() 被上游 Active 节点调用
- **THEN** 帧在调用线程中处理，无额外线程创建，无队列拷贝

### Requirement: EncoderNode encodes frames to compressed packets
系统 SHALL 定义 `EncoderNode`（Transform 类型），使用 FFmpeg `avcodec_send_frame()` / `avcodec_receive_packet()` 将帧编码为压缩包。

EncoderNode SHALL 提供：
- 单个输入端口 + 单个输出端口（帧→包）
- Configure 参数：目标编码器名称（如 "libx264"、"aac"）、编码参数（bitrate, gop_size 等）
- Prepare()：创建 AVCodecContext（编码器模式），打开编码器
- Process(MediaBuffer)：编码帧 → 输出 AVPacket
- ThreadingMode：Active（编码计算密集）

#### Scenario: Encode video frame to H264 packet
- **WHEN** 输入 YUV420P MediaFrame，编码器配置为 "libx264"
- **THEN** 输出 AVPacket（H264 压缩数据），pts 从输入帧继承

#### Scenario: Encode audio frame to AAC packet
- **WHEN** 输入 S16 MediaFrame，编码器配置为 "aac"
- **THEN** 输出 AVPacket（AAC 压缩数据）

#### Scenario: Encoder flushes on EOF
- **WHEN** 输入 kEos 标记的 MediaBuffer
- **THEN** 调用 avcodec_send_frame(nullptr) 冲刷编码器，输出所有延迟帧，最后输出 kEos
