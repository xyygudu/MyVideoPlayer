## Why

当前播放器采用硬编码的 `Demuxer → Decoder → Renderer` 线性管线（PlayerImpl 上帝类编排所有组件），无法在解码和渲染之间插入滤镜、无法组合转码管线、无法替换数据源类型。随着项目向滤镜链、摄像头采集、录屏、转码、推拉流等场景扩展，需要将单体管线重构为可组合的**节点图架构（Media Graph）**，每个处理单元（解码、滤镜、编码等）封装为独立节点，节点间通过端口+链路连接成处理图。

## What Changes

- **BREAKING**: 新增 `MediaGraph` 核心框架（图拓扑管理器 + 节点生命周期编排），替代 `PlayerImpl` 的直接编排逻辑
- **BREAKING**: 新增统一数据载体 `MediaBuffer`（`std::variant<AVPacketPtr, MediaFrame>` + flags + serial），替代当前 PacketQueue/FrameQueue 各自携带不同数据类型的模式
- **BREAKING**: 删除 `StreamContext`（PacketQueue + IDecoder + FrameQueue 硬编码聚合），替换为 Graph 中 Port + Link + DecoderNode 的动态组合
- **BREAKING**: 删除 `IDecoder` 抽象接口，替换为 `INode` 通用节点接口，Decoder 实现为 `DecoderNode`
- 新增 `Link` 模板类（从当前 PacketQueue/FrameQueue 泛化而来），作为节点间异步数据通道
- 新增 `INode` 接口（状态机：Configure → Negotiate → Prepare → Start → Stop），统一所有处理单元的抽象
- 新增 Passive 线程模式：轻量 Transform 节点无独立线程，由上游 Active 节点同步调用 `Process()`，零队列开销
- 新增 `MediaPlayer` 类（高层便捷 API），作为播放场景的预设图构建器，替代 Public API 层的 `Player` 类
- 新增 `AVFilterNode` 包装 FFmpeg libavfilter，支持运行时滤镜链描述字符串（如 `"scale=1280:720,eq=brightness=0.1"`）。滤镜切换采用 Stop→Rebuild→Start 策略（图为静态拓扑）
- 新增 `EncoderNode` / `MuxNode` / `FileSinkNode`，支持文件到文件转码
- Clock 从 StreamContext 级别提升为 MediaGraph 全局时钟，Sink 节点持有引用

## Capabilities

### New Capabilities
- `media-graph-core`: 图拓扑管理（MediaGraph）、统一数据载体（MediaBuffer/MediaFormat）、节点间链路（Link）、端口抽象（Port）
- `graph-node-lifecycle`: 节点状态机（INode）、生命周期回调（Configure/Negotiate/Prepare/Start/Stop）、线程模型（Active/Passive）
- `graph-source-nodes`: DemuxNode（Source 类型，合并了文件打开和解复用），作为图的入口
- `graph-transform-nodes`: Transform 类节点（DecoderNode、AVFilterNode、EncoderNode 等），处理数据
- `graph-sink-nodes`: Sink 类节点（VideoSinkNode、AudioSinkNode、FileSinkNode 等），作为图的出口
- `graph-playback`: MediaPlayer 预设图构建器，替代 Player 类，支持动态插入/替换滤镜链
- `graph-transcode`: Transcoder 预设图构建器，支持文件到文件转码 + 进度回调

### Modified Capabilities
- `stream-context`: **彻底移除**，职责由 MediaGraph + Link + DecoderNode 替代
- `decoder-interface`: **彻底移除** IDecoder，替换为 INode 架构下的 DecoderNode
- `demux-decode`: Demuxer/Decoder 行为逻辑保留，但封装方式从独立类变更为 Node 实现
- `media-frame`: MediaFrame 被 MediaBuffer 包含为 payload 之一（通过 variant），MediaFrame 自身接口不变
- `player-state-machine`: 播放状态管理迁移到 MediaGraph 的 GraphState 枚举
- `playback-control`: Play/Pause/Seek 操作从 PlayerImpl 转移到 MediaGraph + MediaPlayer
- `av-sync`: Clock 从 PlayerImpl 内部转移到 MediaGraph 全局，同步策略（AudioMaster/VideoMaster）保留
- `video-renderer`: VideoRenderer 封装为 VideoSinkNode
- `frame-abstraction`: VideoFrame/AudioFrame 保留为公共 API 类型，内部传输改用 MediaBuffer

## Impact

- **core/include/mvp/graph/** — 新增目录，定义所有图框架的头文件（media_buffer.h, media_format.h, port.h, link.h, node.h, media_graph.h）
- **core/src/graph/** — 新增目录，图框架实现
- **core/src/nodes/** — 新增目录，各节点实现
- **core/include/mvp/player.h** — 删除，替换为 media_player.h
- **core/src/player.cc** — 删除 PlayerImpl
- **core/src/demuxer.h/.cc** — 重构为 DemuxNode（Source 类型，合并文件打开+解复用）
- **core/src/decoder.h/.cc** — 重构为 DecoderNode
- **core/src/i_decoder.h** — 删除
- **core/src/stream_context.h/.cc** — 删除
- **core/src/packet_queue.h/.cc** — 删除，合并到 Link
- **core/src/frame_queue.h/.cc** — 删除，合并到 Link
- **core/CMakeLists.txt** — 更新源文件列表
- **app/** — 使用新的 MediaPlayer API
