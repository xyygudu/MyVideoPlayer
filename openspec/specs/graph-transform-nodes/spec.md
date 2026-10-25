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
- Configure 参数：`EncodeParams`（定义于 `graph-transcode`），含目标编码器名称（如 "libx264"/"aac"）、码率控制模式（CRF 或目标码率）、GOP 大小、最大 B 帧数、编码预设
- ThreadingMode：Active
- Flush()：调用 avcodec_flush_buffers 清空编码器内部缓冲
- EOF 处理：输入端口收到 kEos 时，发送空帧（nullptr）冲刷编码器剩余数据，最后输出 kEos
- Global header：`Negotiate()` SHALL 从下游 caps（`output_port_->Peer()->Caps()`）读取 `HeaderPlacement` 并记录结论；`Prepare()` SHALL 在 `avcodec_open2` 前据此设置或不设置 `AV_CODEC_FLAG_GLOBAL_HEADER`
- 音频帧缓冲：重采样音频 SHALL 经 FIFO 缓冲，按 `codec_ctx_->frame_size` 凑满整帧送入 `avcodec_send_frame`；EOF 时用静音补齐尾帧

EncoderNode 的输出格式为两段式：`Negotiate()` 发布不含 extradata 的初步 EncodedFormat，`Prepare()` 在 `avcodec_open2` 之后重新发布携带真实 codec 参数的最终格式（FFmpeg 的 extradata 仅在编码器打开后可得）。

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

#### Scenario: Encoder applies global header for containers that need it
- **WHEN** 下游 caps 的 HeaderPlacement 为 kGlobal（matroska/mp4）
- **THEN** `avcodec_open2` 前设置 `AV_CODEC_FLAG_GLOBAL_HEADER`，发布格式时 codecpar 携带 extradata

#### Scenario: Encoder keeps in-band headers for TS/AVI
- **WHEN** 下游 caps 的 HeaderPlacement 为 kInBand（mpegts/avi）
- **THEN** 不设置 `AV_CODEC_FLAG_GLOBAL_HEADER`，SPS/PPS 保留在码流内

#### Scenario: Audio encoder gets exactly frame_size samples per frame
- **WHEN** 解码音频帧重采样后 `nb_samples` 不等于 `codec_ctx_->frame_size`（如 AC3 5.1 → 1536 对 AAC 1024）
- **THEN** 编码器将采样缓存入 FIFO，按 `frame_size` 凑满整帧后送入 `avcodec_send_frame`；EOF 时以静音补齐剩余采样

### Requirement: EncoderNode 按 BT.709 编码 RGB 源
输入为 `kRGB32` 而编码器像素格式为 YUV 时，EncoderNode SHALL 在编码器上下文写入 BT.709 色彩标记（色彩空间、原色、传输特性）与有限范围，并以 BT.709 系数、有限范围配置 swscale 的 RGB→YUV 转换；源尺寸或格式变化导致转换上下文重建后 SHALL 重新应用该配置。YUV 输入的行为不变。

#### Scenario: 屏幕录像的色彩标记
- **WHEN** 录制屏幕（BGRA）编码为 H.264
- **THEN** ffprobe 显示 color_space=bt709、color_primaries=bt709、color_transfer=bt709、color_range=tv

### Requirement: EncoderNode 映射硬件编码器的恒定质量
`RateControlMode::kCrf` 时 EncoderNode SHALL 按编码器名映射恒定质量参数：`*_nvenc` 设置 `rc=vbr`、`cq=crf` 并把码率置 0（否则默认 200kb/s 会限制画质）；`*_qsv` 设置 `global_quality=crf`；`*_amf` 设置 `rc=cqp` 与 `qp_i/qp_p/qp_b=crf`；其余编码器设置 `crf`。

#### Scenario: NVENC 恒定质量
- **WHEN** 以 h264_nvenc、crf 23 打开编码器
- **THEN** FFmpeg 日志显示 CQ 模式启用，编码器打开成功

