## Why

最新 commit `3d13970` 内联了 `PlaybackGraphBuilder` 到 `MediaPlayer::Impl`，删除了 `ISourceNode` 接口，重构了 `DemuxNode` 构造和探测逻辑。但代码注释未同步更新，存在以下问题：

- `DemuxNode` 头文件的注释仍引用旧的 `NodeConfig` 配置方式和 `ISourceNode` 生命周期
- `MediaPlayer::BuildGraph` 的注释描述的是基于 `Probe()` + Builder 的流程，而非当前的直连流程
- `node.h` 中 `ISourceNode` 已删除，但其他节点头文件可能仍有相关引用注释
- `DecoderNode` / `VideoSinkNode` / `AudioSinkNode` 的类注释可能未反映当前职责

本次 change 的目标：**仅更新注释，不改代码逻辑**。

## What Changes

- 更新 `demux_node.h` 的类注释：从 `Configure` / `ISourceNode` 引用改为构造器探测 + `StreamInfoMap` 描述
- 更新 `demux_node.cc` 各方法的职责注释，对齐当前实现
- 更新 `media_player.cc` 中 `BuildGraph` 的阶段注释，反映内联后的流程
- 更新 `decoder_node.h` / `video_sink_node.h` / `audio_sink_node.h` 中过时的生命周期或依赖注释
- 梳理 `node.h` 移除 `ISourceNode` 后是否有残留引用

## Capabilities

### New Capabilities
- `code-comment-sync`: 同步代码注释与当前实现，覆盖 media_player.cc、demux_node、decoder_node、sink_node 等被 refactor 影响的文件

### Modified Capabilities
（无 — 本次不修改任何 spec 级的行为定义，仅更新代码注释）

## Impact

- 影响的文件：`src/core/src/` 下的头文件和实现文件注释
- 无 API 变更、无行为变更、无依赖变更
- 编译输出不受影响
