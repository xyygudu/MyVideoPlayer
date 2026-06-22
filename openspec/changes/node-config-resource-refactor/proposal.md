## Why

当前 MediaGraph 节点图架构中，「节点配置 / 流元数据 / 共享资源」三类数据未做本质区分：NodeConfig 是一个包含所有节点字段的上帝结构体（违反接口隔离）；DemuxNode 和 MediaPlayer 各执行一次 avformat_open_input（流参数无合法通道传递，被迫重复打开文件）；HW 加速设备散落在 MediaPlayer 而非 graph 级共享（阻碍未来硬编+硬解共享设备零拷贝）。这三个问题会阻碍向转码、录屏、摄像头采集、推拉流等场景扩展——需要现在解决以建立可扩展的三层模型：构造配置 / 端口协商 / graph 共享资源。

## What Changes

- **BREAKING**: 删除 `NodeConfig` 上帝结构体和 `INode::Configure(const NodeConfig&)` 接口方法。各节点配置改为构造参数或节点专属强类型（如 `DemuxNode(std::string path)`）
- **BREAKING**: 删除 `DecoderNode::SetStream(AVStream*)` 和 `AudioSinkNode::SetStream(AVStream*)`。流参数改为通过 MediaFormat 端口协商自动流动
- `MediaFormat` 扩展：新增 `shared_ptr<AVCodecParameters>` 深拷贝 + `FromStream()` 工厂方法，让 DemuxNode 输出端口携带完整流参数
- `DecoderNode::Negotiate()` 从空壳变为真正实现：从输入端口读取上游 MediaFormat，自我配置 codec
- `OutputPort::Connect()` 新增格式传播：将输出端口的 MediaFormat 同步到下游输入端口
- `MediaGraph` 新增 `SetHWDevice/HWDevice()` 共享资源接口（与 Clock 平行），DecoderNode 从 graph 查询 HW 设备
- `MediaPlayer::BuildGraph()` 大幅简化：删除第二次 avformat_open_input，删除手动 SetStream/SetHWAccel 调用

## Capabilities

### New Capabilities
- `port-format-negotiation`: 端口格式协商机制——MediaFormat 携带 AVCodecParameters 拷贝，Connect 时传播到下游，Negotiate 阶段节点从输入端口自我配置
- `graph-shared-resources`: Graph 级共享资源机制——HW 设备作为 MediaGraph 共享资源，与 Clock 平行设计，需要的节点从 graph 查询

### Modified Capabilities
- `media-graph-core`: MediaFormat 扩展（新增 codec_params 字段）；INode 接口移除 Configure；Port::Connect 新增格式传播
- `demux-decode`: DemuxNode 输出端口携带完整流参数；DecoderNode 从端口协商自我配置，移除 SetStream
- `hw-accel`: HWAccelContext 从 MediaPlayer 成员提升为 MediaGraph 共享资源

## Impact

- **graph/media_format.h/.cc** — 扩展（新增 AVCodecParameters 存储 + FromStream 工厂）
- **graph/node.h** — 删除 NodeConfig 和 Configure 接口
- **graph/port.h/.cc** — Connect 时传播格式
- **graph/media_graph.h/.cc** — 新增 HWDevice 共享资源
- **nodes/demux_node.h/.cc** — 构造注入路径，输出端口携带完整参数
- **nodes/decoder_node.h/.cc** — Negotiate 真正实现，移除 SetStream/SetHWAccel
- **nodes/video_sink_node.h/.cc** — 移除 Configure
- **nodes/audio_sink_node.h/.cc** — 移除 Configure/SetStream，参数从端口格式读取
- **media_player.cc** — BuildGraph 大幅简化
