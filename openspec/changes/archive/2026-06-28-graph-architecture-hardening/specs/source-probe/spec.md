## ADDED Requirements

### Requirement: ISourceNode 探测接口
系统 SHALL 定义 `ISourceNode : public INode`，提供纯虚方法 `virtual std::vector<StreamInfo> Probe() = 0`，用于在建图前发现源的流拓扑。

```cpp
struct StreamInfo {
    int index;
    MediaType type;
    MediaFormat format;   // 携带 EncodedFormat（含 codec_params 拷贝）
    double duration{0.0}; // 源时长（秒）
};
```

Probe SHALL 打开源并返回所有可用流的描述。Probe SHALL 幂等：重复调用不重复打开。

#### Scenario: DemuxNode Probe 返回流信息
- **WHEN** DemuxNode::Probe() 在含 1 视频 1 音频的文件上调用
- **THEN** 返回 2 个 StreamInfo，分别含 codec_params、time_base、duration

#### Scenario: Probe 携带 duration
- **WHEN** Probe() 成功
- **THEN** 每个 StreamInfo 的 duration 字段为源时长，供 MediaPlayer 缓存

### Requirement: 探测独立于图统一生命周期
源探测 SHALL 作为独立于图 Negotiate/Prepare 的前置阶段。MediaPlayer SHALL 先 Probe 源、用 StreamInfo 构建拓扑，然后图统一 Negotiate→Prepare。

DemuxNode SHALL 不再在 BuildGraph 里手动先 Negotiate+Prepare。其 Prepare SHALL 幂等（format_ctx_ 判空守卫），可在图统一 Prepare 阶段安全重入。

#### Scenario: 探测先于建图
- **WHEN** MediaPlayer::Open(path)
- **THEN** 顺序为：source->Probe() → builder 建图 → graph->Negotiate() → graph->Prepare()

#### Scenario: DemuxNode Prepare 幂等
- **WHEN** Probe 已打开文件，图统一 Prepare 再次调用 DemuxNode::Prepare
- **THEN** 检测到 format_ctx_ 非空，跳过重复打开，只创建输出端口

### Requirement: MediaPlayer 缓存源时长
MediaPlayer SHALL 从 Probe 返回的 StreamInfo 缓存 duration，Duration() 查询 SHALL 读缓存值，不再调用 demux_node_->Duration()。

#### Scenario: Duration 读缓存不触碰节点
- **WHEN** MediaPlayer::Duration() 被调用
- **THEN** 返回 Probe 阶段缓存的 duration 值，不访问任何节点
