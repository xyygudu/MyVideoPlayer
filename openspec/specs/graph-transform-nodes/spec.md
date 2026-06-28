## Purpose

Defines the transform nodes (AVFilterNode, EncoderNode) that process media
data mid-pipeline — applying FFmpeg libavfilter graphs and encoding frames.

## Requirements

### Requirement: AVFilterNode wraps FFmpeg libavfilter graph
系统 SHALL 定义 `AVFilterNode`（Transform 类型），包装 FFmpeg 的 `AVFilterGraph` + `buffersrc`/`buffersink` 对。

AVFilterNode SHALL 提供：
- 单个输入端口 + 单个输出端口
- Configure 参数：滤镜描述字符串（如 `"scale=1280:720,eq=brightness=0.1"`）
- Negotiate()：根据输入 MediaFormat 配置 buffersrc，从 buffersink 读取输出格式
- ThreadingMode：Passive（默认）

#### Scenario: Apply scale filter on video frame
- **WHEN** AVFilterNode 配置 "scale=1280:720"，输入 1920×1080 的帧
- **THEN** Process() 输出 1280×720 的帧

#### Scenario: Process on upstream thread (Passive mode)
- **WHEN** Passive AVFilterNode 的 Process() 被上游 Active 节点调用
- **THEN** 帧在调用线程中处理，无额外线程创建

### Requirement: EncoderNode encodes frames to compressed packets
系统 SHALL 定义 `EncoderNode`（Transform 类型），使用 FFmpeg `avcodec_send_frame()` / `avcodec_receive_packet()` 编码。

EncoderNode SHALL 提供：
- 单个输入端口 + 单个输出端口（帧→包）
- Configure 参数：目标编码器名称（如 "libx264"）、编码参数
- ThreadingMode：Active

#### Scenario: Encode video frame to H264 packet
- **WHEN** 输入 YUV420P MediaFrame，编码器配置为 "libx264"
- **THEN** 输出 AVPacket（H264 压缩数据）
