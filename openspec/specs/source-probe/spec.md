## Purpose

Defines the ISourceNode probe interface and StreamInfo struct that enable
pre-graph source inspection, separating topology discovery from uniform
graph lifecycle.

## Requirements

### Requirement: ISourceNode 探测接口
系统 SHALL 定义 `ISourceNode : public INode`，提供纯虚方法 `virtual std::vector<StreamInfo> Probe() = 0`。

```cpp
struct StreamInfo {
    int index;
    MediaType type;
    MediaFormat format;
    double duration{0.0};
};
```

Probe SHALL 打开源并返回所有可用流的描述。Probe SHALL 幂等：重复调用不重复打开。

#### Scenario: DemuxNode Probe 返回流信息
- **WHEN** DemuxNode::Probe() 在含 1 视频 1 音频的文件上调用
- **THEN** 返回 2 个 StreamInfo，分别含 codec_params、time_base、duration

### Requirement: 探测独立于图统一生命周期
源探测 SHALL 作为独立于图 Negotiate/Prepare 的前置阶段。MediaPlayer SHALL 先 Probe 源、用 StreamInfo 构建拓扑，然后图统一 Negotiate→Prepare。

DemuxNode::Prepare SHALL 幂等（format_ctx_ 判空守卫），可在图统一 Prepare 阶段安全重入。

#### Scenario: 探测先于建图
- **WHEN** MediaPlayer::Open(path)
- **THEN** 顺序为：source->Probe() → builder 建图 → graph->Negotiate() → graph->Prepare()

### Requirement: MediaPlayer 缓存源时长
MediaPlayer SHALL 从 Probe 返回的 StreamInfo 缓存 duration，Duration() 查询 SHALL 读缓存值。

#### Scenario: Duration 读缓存不触碰节点
- **WHEN** MediaPlayer::Duration() 被调用
- **THEN** 返回 Probe 阶段缓存的 duration 值，不访问任何节点
