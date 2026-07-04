# code-comment-sync Specification

## Purpose
TBD - created by archiving change sync-code-comments. Update Purpose after archive.
## Requirements
### Requirement: DemuxNode 类注释反映构造器探测模式
DemuxNode 的类注释 SHALL 更新，移除对 `NodeConfig::file_path` 和 `ISourceNode::Probe` 的引用，描述当前的构造器探测模式：`explicit DemuxNode(std::string file_path)` 在构造时调用 `InitStreamInfo()` 自动打开文件和创建输出端口。

#### Scenario: 类注释描述构造器
- **WHEN** 阅读 `demux_node.h` 的类注释
- **THEN** 注释描述构造器注入文件路径和 InitStreamInfo 自动探测

### Requirement: DemuxNode 方法注释对齐当前职责
DemuxNode::OpenFile / FindStreams / MakeStreamFormat / InitStreamInfo / Outputs 的方法注释 SHALL 更新，准确反映每个方法的职责和调用关系。

#### Scenario: InitStreamInfo 注释描述探测行为
- **WHEN** 阅读 InitStreamInfo 的注释
- **THEN** 注释说明它在构造时被调用、打开文件、发现流、填充 stream_info_map_

### Requirement: MediaPlayer::BuildGraph 阶段注释反映直连流程
`BuildGraph` 内的阶段注释 SHALL 更新，描述当前的三阶段直连流程：
1. 构造 DemuxNode（自动探测）→ 获取 stream_info_map_
2. 遍历流 → 创建 decoder/sink → AddNode → 配置 → Connect（硬编码端口索引）
3. graph_->Negotiate() → graph_->Prepare()

#### Scenario: 注释对齐三阶段
- **WHEN** 阅读 BuildGraph 中的阶段注释
- **THEN** 注释准确描述探测→创建/配置→生命周期三个阶段

### Requirement: 被影响头文件类注释检查更新
decoder_node.h / video_sink_node.h / audio_sink_node.h 的类注释 SHALL 检查，移除对已删除机制（如 `PlaybackGraphBuilder`、`ISourceNode`、旧 `NodeConfig`）的任何引用。

#### Scenario: 无残留引用
- **WHEN** grep 搜索 "PlaybackGraphBuilder|ISourceNode|Probe" 在头文件中
- **THEN** 仅在与当前实现一致的地方出现

