## MODIFIED Requirements

### Requirement: EncoderNode encodes frames to compressed packets
系统 SHALL 定义 `EncoderNode`（Transform 类型），使用 FFmpeg `avcodec_send_frame()` / `avcodec_receive_packet()` 编码。

EncoderNode SHALL 提供：
- 单个输入端口 + 单个输出端口（帧→包）
- Configure 参数：`EncodeParams`（定义于 `graph-transcode`），含目标编码器名称（如 "libx264"/"aac"）、码率控制模式（CRF 或目标码率）、GOP 大小、最大 B 帧数、编码预设
- ThreadingMode：Active
- Flush()：调用 avcodec_flush_buffers 清空编码器内部缓冲
- EOF 处理：输入端口收到 kEos 时，发送空帧（nullptr）冲刷编码器剩余数据，最后输出 kEos

#### Scenario: Encode video frame to H264 packet
- **WHEN** 输入 YUV420P MediaFrame，编码器配置为 "libx264"
- **THEN** 输出 AVPacket（H264 压缩数据）

#### Scenario: CRF rate control mode
- **WHEN** EncodeParams.rate_control 为 kCrf，crf 值为 23
- **THEN** 编码器以恒定质量模式编码，不设置固定目标码率

#### Scenario: Bitrate rate control mode
- **WHEN** EncodeParams.rate_control 为 kBitrate，bitrate_bps 为 2000000
- **THEN** AVCodecContext::bit_rate 被设置为 2000000，编码器以目标码率模式编码

#### Scenario: EOS propagates through encoder
- **WHEN** 输入端口收到 flags=kEos 的 MediaBuffer
- **THEN** 调用 avcodec_send_frame(nullptr) 冲刷编码器缓冲，最后输出端口 Push 一个 flags=kEos 的 MediaBuffer
