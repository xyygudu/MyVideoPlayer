## MODIFIED Requirements

### Requirement: DecoderNode Negotiate 做格式推理
DecoderNode::Negotiate SHALL 从输入端口的 EncodedFormat::codec_params 推理输出格式，不开 codec。利用 AVCodecParameters 自带的 width/height/format/sample_rate 直接构造输出 VideoFormat/AudioFormat 并设置到输出端口。

DecoderNode::Prepare SHALL 只剩资源分配（avcodec_find_decoder + avcodec_open2 + HW 配置），不再设置输出格式。

#### Scenario: Negotiate 算出输出格式不开 codec
- **WHEN** DecoderNode::Negotiate 执行
- **THEN** 从输入端口 codec_params 读 width/height，构造 VideoFormat 设到输出端口，未调用 avcodec_open2

#### Scenario: Prepare 只分配资源
- **WHEN** DecoderNode::Prepare 执行
- **THEN** 仅查找并打开 codec、配置 HW，不触碰输出端口格式

#### Scenario: 格式不兼容可在 Negotiate 失败
- **WHEN** 上游格式与本节点能力不符（未来滤镜场景）
- **THEN** Negotiate 可返回 false 快速失败，未分配任何 codec/device 资源

### Requirement: DemuxNode 实现 ISourceNode::Probe
DemuxNode SHALL 实现 ISourceNode，提供 Probe() 返回 StreamInfo 列表。Probe SHALL 打开文件、发现流、构造每流的 EncodedFormat（含 codec_params 拷贝）和 duration。

DemuxNode::Prepare SHALL 幂等：若 Probe 已打开 format_ctx_，则跳过重复打开，只创建输出端口。

#### Scenario: Probe 发现流
- **WHEN** DemuxNode::Probe 在含音视频的文件上调用
- **THEN** 返回每个流的 StreamInfo（type/EncodedFormat/duration）

#### Scenario: Prepare 不重复打开
- **WHEN** Probe 后图统一 Prepare 调用 DemuxNode::Prepare
- **THEN** format_ctx_ 已存在，仅建端口

### Requirement: DecodeLoop 消除 goto
DecoderNode::DecodeLoop SHALL 不使用 goto。EOS 处理逻辑 SHALL 提炼为独立方法（MaybeFlushOnSerialChange / ProcessPacket / HandleEos），EOS 分支处理完后 continue 回循环顶部正常 Pull，不内嵌处理新数据。

#### Scenario: 无 goto 的解码循环
- **WHEN** DecodeLoop 处理 EOS 后收到新数据
- **THEN** EOS 分支 drain 后 continue，新数据走正常 Pull 路径，无 goto 跳转

### Requirement: 节点长函数提炼至 50 行内
DemuxNode::Prepare/DemuxLoop、DecoderNode::Prepare/DecodeLoop、VideoSinkNode::RenderLoop/ComputeDisplayDelay、AudioSinkNode::AudioLoop/Prepare SHALL 提炼私有辅助方法，每个函数体不超过 50 行。

#### Scenario: AudioLoop 提炼
- **WHEN** AudioSinkNode::AudioLoop 重构后
- **THEN** 提炼 WaitForBufferSpace/ConvertAndFeed/DrainAndReportEos，主循环为薄分发

#### Scenario: ComputeDisplayDelay 按模式拆分
- **WHEN** VideoSinkNode 计算显示延迟
- **THEN** 拆为 ComputeAudioMasterDelay/ComputeVideoMasterDelay，主方法按 sync_mode 分派

### Requirement: 节点响应 OnCommand
DemuxNode/DecoderNode/AudioSinkNode SHALL 覆写 OnCommand 响应 kSeek：DemuxNode 重定位、DecoderNode 设 drop_until_pts、AudioSinkNode 清 SDL 缓冲。

#### Scenario: 节点自主响应 seek
- **WHEN** 各节点收到 OnCommand({kSeek, pos})
- **THEN** DemuxNode RequestSeek、DecoderNode SetDropUntilPts、AudioSinkNode FlushSdlBuffer
