## ADDED Requirements

### Requirement: Command 事件机制
系统 SHALL 定义 `Command` 结构体和 `CommandType` 枚举，作为图中高层控制意图的载体。

```cpp
enum class CommandType { kSeek };   // 当前仅 kSeek；后续扩展加枚举值，不改接口
struct Command { CommandType type; double position{0.0}; };
```

Command SHALL 只表达高层控制意图，不表达机制步骤。flush、drop_until_pts 等机制 SHALL 由节点响应命令时内部处理，不作为独立命令。

#### Scenario: 加新命令不改接口
- **WHEN** 未来需要支持暂停步进
- **THEN** 仅在 CommandType 枚举增加 kStepFrame 值，INode::OnCommand 签名不变

### Requirement: INode 响应命令
INode SHALL 提供虚方法 `virtual void OnCommand(const Command& cmd) {}`，默认空实现。各节点 SHALL 按需覆写，自己决定如何响应命令（机制下沉）。

#### Scenario: DemuxNode 响应 kSeek
- **WHEN** DemuxNode 收到 OnCommand({kSeek, pos})
- **THEN** 内部调用 RequestSeek(pos) 重定位

#### Scenario: DecoderNode 响应 kSeek
- **WHEN** DecoderNode 收到 OnCommand({kSeek, pos})
- **THEN** 内部调用 SetDropUntilPts(pos)；codec flush 由解码线程检测 serial 变化自行处理

#### Scenario: AudioSinkNode 响应 kSeek
- **WHEN** AudioSinkNode 收到 OnCommand({kSeek, pos})
- **THEN** 内部清空 SDL 音频缓冲

### Requirement: MediaGraph 高层控制操作
MediaGraph SHALL 提供 `Seek(double position)` 和 `SetPaused(bool paused)` 高层操作，封装节点编排，使外部无需持有单个节点。

`Seek()` SHALL 先调用 Flush()（清所有 Link 队列 + serial++），再 SendCommand({kSeek, position}) 按拓扑序分发给所有节点。

`SetPaused()` SHALL 按拓扑序对所有节点调用 SetPaused，实现状态级联。

`SendCommand(const Command&)` SHALL 按拓扑序将命令分发给所有节点（节点自己过滤是否响应）。

#### Scenario: Graph Seek 协调 flush 与命令
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** 先 Flush 所有 Link，再向所有节点广播 {kSeek, 30.0}

#### Scenario: 命令广播 + 节点过滤
- **WHEN** SendCommand 分发 {kSeek, pos}
- **THEN** 每个节点收到命令，不响应的节点（如 VideoSinkNode 对 kSeek）默认空实现忽略

### Requirement: MediaPlayer 不持有单个节点
MediaPlayer::Impl SHALL 删除所有单节点成员指针（demux_node_/video_decoder_/audio_decoder_/video_sink_/audio_sink_）。控制操作 SHALL 通过 graph 高层操作实现。

#### Scenario: MediaPlayer Seek 不触碰节点
- **WHEN** MediaPlayer::Seek(t) 被调用
- **THEN** 仅调用 graph_->Seek(t) + 重置 clock，不直接调用任何节点方法

#### Scenario: 加滤镜节点不改 MediaPlayer
- **WHEN** 未来在 Decoder 和 Sink 之间插入滤镜节点
- **THEN** MediaPlayer 的 Seek/Pause/Play 逻辑无需修改
