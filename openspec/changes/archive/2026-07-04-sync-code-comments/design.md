## Context

commit `3d13970` 做了以下重构：
1. 删除了 `PlaybackGraphBuilder`（`playback_graph_builder.h/.cc`），管线构建逻辑内联回 `MediaPlayer::Impl::BuildGraph`
2. 删除了 `ISourceNode` 接口（`node.h`），DemuxNode 不再实现 `Probe()` 接口，改由构造函数调用 `InitStreamInfo()` 自动探测
3. DemuxNode 新增 `StreamInfoMap()` 访问器，MediaPlayer 通过它获取流拓扑信息

代码注释未同步更新：头文件中仍有对旧模式（`ISourceNode::Probe`、`PlaybackGraphBuilder`、`NodeConfig`）的引用，`BuildGraph` 的阶段注释与当前直连流程不符。

## Goals / Non-Goals

**Goals:**
- 更新 `demux_node.h` 类注释和生命周期描述，反映构造器探测 + `StreamInfoMap` 模式
- 更新 `media_player.cc` 的 `BuildGraph` 阶段注释，对齐内联后的直连流程
- 更新 `decoder_node.h` / `video_sink_node.h` / `audio_sink_node.h` 中过时的职责注释
- 确保所有注释准确描述当前实现，不误导读者

**Non-Goals:**
- 不改任何代码逻辑、不新增接口
- 不修改 spec 文档（本次 change 不涉及行为变更）

## Decisions

### Decision 1: 按"头文件类注释 → 方法注释 → 实现注释"逐文件覆盖
仅更新与当前实现不一致的注释，保留正确的部分。具体顺序：

1. `demux_node.h` — 类注释重写（去掉 `Configure` 引用、`ISourceNode` 引用，描述构造器探测）
2. `demux_node.cc` — `InitStreamInfo` 和构造器注释，各 helper 职责
3. `media_player.cc` — `BuildGraph` 的阶段注释（三阶段：探测→创建/配置→Negotiate/Prepare）
4. `decoder_node.h` / `video_sink_node.h` / `audio_sink_node.h` — 类注释，检查过时引用

### Decision 2: 不引入新的注释风格
保持现有的 `///` Doxygen 风格和类注释格式，仅修正内容。

## Risks / Trade-offs

- **[低风险]** 可能会漏掉某些过时注释 → 通过 `git grep "Probe\|PlaybackGraphBuilder\|NodeConfig\|ISourceNode"` 搜索残留引用
- **[无风险]** 纯注释变更，不影响编译和运行
