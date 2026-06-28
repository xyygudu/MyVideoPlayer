## Why

当前 MediaGraph 节点图已能播放，但存在四个架构缺陷和一批代码质量问题，会阻碍向滤镜链、转码、摄像头、推拉流等场景扩展。四个缺陷指向同一病根——**MediaGraph 抽象太弱，编排逻辑泄漏到了 MediaPlayer**：DemuxNode 在 BuildGraph 里手动先 Negotiate+Prepare 破坏统一生命周期（Q1）；DecoderNode::Negotiate 名不副实只缓存数据（Q2）；MediaFormat 是音视频混杂的胖结构体（Q3）；MediaPlayer 持有所有 node 成员做细粒度逐节点编排（Q4）。同时存在 10 个超 50 行函数和 1 处 goto。现在解决以建立可扩展的三层模型：构造配置 / 端口协商 / graph 共享资源。

## What Changes

- **BREAKING**: MediaFormat 用 `std::variant<EncodedFormat, VideoFormat, AudioFormat>` 重构，拆分音视频字段，codec_params 移入 EncodedFormat 分支（不再做公共字段）。调用点从 `fmt.width()` 改为 `fmt.AsVideo().width`（强制类型确立，不保留转发）
- **BREAKING**: MediaPlayer::Impl 删除所有单节点成员（demux_node_/video_decoder_/audio_decoder_/video_sink_/audio_sink_），改为通过 graph 事件控制
- 新增 Command 事件机制：`MediaGraph::Seek()` = Flush + SendCommand(kSeek)；节点实现 `OnCommand()` 自己响应（机制下沉）。当前只实现 kSeek
- 新增 `MediaGraph::SetPaused()` 状态级联，替代 MediaPlayer 逐个调用 sink->SetPaused
- DecoderNode::Negotiate 改为真正格式推理：从 codecpar 算出输出格式（不开 codec）；Prepare 只剩资源分配
- 新增 `ISourceNode::Probe()` 源探测阶段，返回 StreamInfo 列表（含 duration），形式化"先解析源"约束，移出图统一生命周期
- 新增 `PlaybackGraphBuilder`（滤镜就绪的链式 `AddVideoPipeline(stream, filters={})`），BuildGraph 从 104 行收缩到 ~15 行
- 消除 DecodeLoop 的 goto，提炼所有 10 个超 50 行函数

## Capabilities

### New Capabilities
- `graph-command-control`: 事件化控制——Command/OnCommand/SendCommand 机制，MediaPlayer 通过 graph 高层操作控制，不持有单个节点
- `source-probe`: 源探测阶段——ISourceNode::Probe 返回 StreamInfo，形式化拓扑发现，独立于图生命周期
- `playback-graph-builder`: 滤镜就绪的播放图构建器，封装链式管线组装，预留转码复用

### Modified Capabilities
- `media-graph-core`: MediaFormat variant 重构；INode 新增 OnCommand；MediaGraph 新增 Seek/SetPaused/SendCommand
- `demux-decode`: DecoderNode::Negotiate 改为格式推理；DemuxNode 实现 Probe；长函数提炼 + 消 goto

## Impact

- **graph/media_format.h/.cc** — variant 重构（EncodedFormat/VideoFormat/AudioFormat）+ Intersect 泛型化
- **graph/graph_command.h**（新增）— Command/CommandType 定义
- **graph/node.h** — INode 新增 OnCommand；ISourceNode + Probe；StreamInfo
- **graph/media_graph.h/.cc** — Seek/SetPaused/SendCommand + duration 元数据
- **nodes/demux_node.h/.cc** — Probe + OnCommand + Prepare/DemuxLoop 提炼
- **nodes/decoder_node.h/.cc** — Negotiate 设输出格式 + OnCommand + DecodeLoop 消 goto + Prepare 提炼
- **nodes/video_sink_node.h/.cc** — OnCommand + RenderLoop/ComputeDisplayDelay 提炼
- **nodes/audio_sink_node.h/.cc** — OnCommand + AudioLoop/Prepare 提炼
- **nodes/playback_graph_builder.h/.cc**（新增）— 封装建图
- **media_player.cc** — 删除所有单节点成员，事件控制，BuildGraph 瘦身
- 所有 `fmt.width()` 等调用点迁移到 `AsVideo()/AsAudio()`
