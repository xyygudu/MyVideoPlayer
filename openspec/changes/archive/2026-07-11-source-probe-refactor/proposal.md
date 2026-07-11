## Why

`BuildGraph` 当前承担了探帧、提取元数据、创建节点、配置 Sink、连线、生命周期管理六项职责，函数体 ~90 行。更根本的问题是：探帧（DemuxNode 构造的副作用）与后续节点创建之间存在数据依赖——必须先从 DemuxNode 拿到 fps/has_audio 等信息，才能配置 VideoSinkNode。这导致节点创建和配置无法内聚在一起。

## What Changes

- **新增** `SourceProbe::Probe(filepath) → SourceInfo`：独立于 Graph 的纯探帧工具，打开文件 → 读取流信息 → 关闭文件
- **新增** `SourceInfo` 通用结构体：包含文件级信息（filepath、duration、format_name）和流级信息（VideoStream、AudioStream 子结构）
- **修改** `BuildGraph`：拆为"探帧 → 建图 → 加节点（含配置）→ 连线 → 完成"五个阶段，~35 行
- **移除** `DemuxNode` 对外暴露 `StreamInfoMap()` 的依赖（`BuildGraph` 不再从 DemuxNode 获取流信息）
- **不变**：SinkNode 保持 Setter 模式配置，连线保持 inline 不封装

## Capabilities

### New Capabilities
- `source-info`: 通用的 SourceInfo 结构体，描述媒体文件的容器信息及所有音视频流属性，可复用于 UI 流列表展示、流切换、CanPlay 快速判断等场景

### Modified Capabilities
- `source-probe`: 将 ISourceNode::Probe() 接口替换为独立工具类 SourceProbe::Probe()，返回更丰富的 SourceInfo 而非简单 StreamInfo 列表；StreamInfo 升级为 SourceInfo
- `playback-graph-builder`: 简化——不引入 PlaybackGraphBuilder 类或 PlaybackContext，仅通过预探帧消除 BuildGraph 内的数据依赖，保持函数内聚

## Impact

| 文件 | 影响 |
|------|------|
| `src/core/include/mvp/source_info.h` | **新增** SourceInfo / VideoStream / AudioStream 结构体 |
| `src/core/include/mvp/source_probe.h` | **新增** SourceProbe 类声明 |
| `src/core/src/source_probe.cc` | **新增** SourceProbe::Probe() 实现 |
| `src/core/src/media_player.cc` | **修改** BuildGraph 重写，移除对 DemuxNode::StreamInfoMap() 的依赖 |
| `src/core/src/nodes/demux_node.cc/h` | **可能修改** 若 StreamInfoMap() 无其他调用方可移除 |
| `src/core/CMakeLists.txt` | **修改** 添加新源文件 |
