## ADDED Requirements

### Requirement: MediaFormat carries codec parameters as shared copy
MediaFormat SHALL 持有 `std::shared_ptr<AVCodecParameters>` 成员（深拷贝 + 自定义 deleter），代表该端口传输的流的编码参数。

MediaFormat SHALL 提供 `FromStream()` 工厂方法，从 `AVCodecParameters*` 深拷贝构造，使用 `avcodec_parameters_alloc` + `avcodec_parameters_copy`，deleter 使用 `avcodec_parameters_free`。

MediaFormat SHALL 提供 `codec_params()` 访问器返回 `const AVCodecParameters*`。

#### Scenario: DemuxNode fills codec params on output port
- **WHEN** DemuxNode::Prepare() 发现视频流
- **THEN** 输出端口的 MediaFormat 通过 FromStream() 携带该流的完整 AVCodecParameters 拷贝

#### Scenario: MediaFormat copy is cheap (shared_ptr)
- **WHEN** MediaFormat 被拷贝（如 SetFormat 值传递）
- **THEN** 内部 codec_params_ 仅增加引用计数，不 deep copy AVCodecParameters

#### Scenario: Codec params lifetime independent of DemuxNode
- **WHEN** DemuxNode 被析构
- **THEN** 下游节点持有的 MediaFormat 中 codec_params 仍有效（shared_ptr 引用计数管理）

### Requirement: OutputPort::Connect propagates format to downstream InputPort
OutputPort::Connect() SHALL 在建立连接后调用 `peer->SetFormat(format_)` 将上游输出格式传播到下游输入端口。

#### Scenario: Downstream reads format after connect
- **WHEN** DemuxNode OutputPort 连接到 DecoderNode InputPort
- **THEN** DecoderNode 的 InputPort::Format() 返回 DemuxNode 输出端口的 MediaFormat（含完整 codec params）

### Requirement: Downstream node negotiates from input port format
下游节点的 Negotiate() SHALL 从 InputPort::Format() 读取上游格式并自我配置，不再需要外部手动设置参数（如 SetStream）。

#### Scenario: DecoderNode self-configures from port format
- **WHEN** MediaGraph::Negotiate() 按拓扑序调用 DecoderNode::Negotiate()
- **THEN** DecoderNode 从 input_port_->Format().codec_params() 获取编码参数，缓存供 Prepare() 使用

#### Scenario: AudioSinkNode reads sample rate from port format
- **WHEN** AudioSinkNode::Negotiate() 被调用
- **THEN** 从 input_port_->Format().codec_params() 读取 sample_rate 和 channels，无需外部 SetStream
