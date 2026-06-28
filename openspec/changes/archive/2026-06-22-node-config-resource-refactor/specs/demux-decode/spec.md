## MODIFIED Requirements

### Requirement: DemuxNode uses constructor injection for file path
DemuxNode SHALL 通过构造函数接收文件路径 `explicit DemuxNode(std::string file_path)`，移除对 NodeConfig 的依赖。

DemuxNode::Prepare() SHALL 使用构造时保存的路径打开文件（avformat_open_input），并为每个输出端口设置 MediaFormat::FromStream()（携带完整 AVCodecParameters 拷贝）。

#### Scenario: DemuxNode constructed with path
- **WHEN** `auto demux = std::make_unique<DemuxNode>("/path/to/file.mp4")`
- **THEN** DemuxNode 内部保存路径，Prepare() 时使用

#### Scenario: Output ports carry full codec params
- **WHEN** DemuxNode::Prepare() 成功
- **THEN** 每个输出端口的 MediaFormat 包含该流的 AVCodecParameters 深拷贝 + codec_id + time_base + frame_rate

### Requirement: DecoderNode self-configures via Negotiate
DecoderNode::Negotiate() SHALL 从 input_port_->Format().codec_params() 读取编码参数，缓存 `const AVCodecParameters*` 指针（生命周期由 shared_ptr 保证）供 Prepare() 使用。

DecoderNode SHALL 移除 `SetStream(AVStream*)` 方法和 `stream_` 成员。

DecoderNode::Prepare() SHALL 使用 Negotiate 阶段缓存的 codecpar 打开解码器（avcodec_find_decoder + avcodec_parameters_to_context）。

#### Scenario: DecoderNode opens codec from negotiated params
- **WHEN** DecoderNode::Prepare() 执行
- **THEN** 使用 Negotiate 阶段从输入端口获取的 codec_id 查找解码器，从 codecpar 填充 codec context

#### Scenario: No more SetStream dependency
- **WHEN** 构建播放图
- **THEN** DecoderNode 仅需 AddNode + Connect + graph Negotiate/Prepare，无需手动 SetStream

### Requirement: DecoderNode queries HW device from graph
DecoderNode SHALL 移除 `SetHWAccel(HWAccelContext*)` 方法和 `hw_ctx_` 配置时设置。

DecoderNode::Prepare() SHALL 通过 `graph_->HWDevice()` 查询 HW 加速上下文（仅视频解码时查询），若返回有效指针则配置硬件加速，否则静默回退到软解。

#### Scenario: HW accel from graph resource
- **WHEN** DecoderNode（视频）Prepare 且 graph 已 SetHWDevice
- **THEN** 使用 graph 的 HWAccelContext 配置 codec_ctx 硬件加速

#### Scenario: Graceful fallback without HW
- **WHEN** graph 未 SetHWDevice 或返回 nullptr
- **THEN** 解码器以软解模式打开，无错误日志

### Requirement: AudioSinkNode reads params from port format
AudioSinkNode SHALL 移除 `SetStream(AVStream*)` 方法和 `stream_` 成员。

AudioSinkNode::Negotiate() 或 Prepare() SHALL 从 input_port_->Format().codec_params() 读取 sample_rate 和 ch_layout.nb_channels 以配置 SDL 音频设备。

#### Scenario: AudioSinkNode configures from codec params
- **WHEN** AudioSinkNode::Prepare() 执行
- **THEN** 从输入端口的 MediaFormat codec_params 读取采样率和声道数，配置 SDL audio spec
