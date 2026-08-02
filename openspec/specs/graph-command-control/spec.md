## Purpose

Defines the Command event mechanism for high-level control intent (seek, pause)
propagated through the graph. Commands express intent without prescribing
mechanism; each node decides how to respond.

## Requirements

### Requirement: Command 事件机制
系统 SHALL 定义 `Command` 结构体和 `CommandType` 枚举，作为图中高层控制意图的载体。

```cpp
enum class CommandType { kSeek, kRedraw };
struct Command { CommandType type; double position{0.0}; };
```

Command SHALL 只表达高层控制意图，不表达机制步骤。flush、drop_until_pts 等机制 SHALL 由节点响应命令时内部处理。

#### Scenario: 加新命令不改接口
- **WHEN** 未来需要支持暂停步进
- **THEN** 仅在 CommandType 枚举增加新值，INode::OnCommand 签名不变

#### Scenario: 不关心的节点忽略新意图
- **WHEN** 广播 {kRedraw} 到全图
- **THEN** DemuxNode / DecoderNode / AudioSinkNode 等无重绘概念的节点默认无动作

### Requirement: kRedraw 表达重新呈现当前画面的意图
系统 SHALL 定义 `CommandType::kRedraw`，表示"以当前窗口状态重新呈现最后一帧"。

窗口尺寸变化时，facade SHALL 通过 graph 广播该意图，SHALL NOT 直接持有 sink 节点指针。

#### Scenario: 暂停态窗口缩放立即重新布局
- **WHEN** 播放暂停时拖拽窗口改变尺寸
- **THEN** VideoSinkNode 收到 kRedraw 并以新尺寸重绘当前帧，画面立即重新适配，无需恢复播放

#### Scenario: facade 不触碰节点
- **WHEN** MediaPlayer 处理窗口尺寸变化
- **THEN** 仅调用 `graph_->SendCommand({kRedraw})`，不引用任何具体节点类型

### Requirement: INode 响应命令
INode SHALL 提供虚方法 `virtual void OnCommand(const Command& cmd) {}`，默认空实现。各节点 SHALL 按需覆写。

#### Scenario: DemuxNode 响应 kSeek
- **WHEN** DemuxNode 收到 OnCommand({kSeek, pos})
- **THEN** 内部调用 RequestSeek(pos) 重定位

#### Scenario: AudioSinkNode 响应 kSeek
- **WHEN** AudioSinkNode 收到 OnCommand({kSeek, pos})
- **THEN** 内部清空 SDL 音频缓冲

### Requirement: MediaGraph 高层控制操作
MediaGraph SHALL 提供 `Seek(double position)` 和 `SetPaused(bool paused)` 高层操作。`Seek()` SHALL 依次递增 seek 世代、Flush()、SendCommand({kSeek, position})、重置时钟。`SendCommand(const Command&)` SHALL 按拓扑序分发。

#### Scenario: Graph Seek 协调世代、flush 与命令
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** 先递增世代，再 Flush 所有 Link，然后向所有节点广播 {kSeek, 30.0}

### Requirement: MediaPlayer 不持有单个节点
MediaPlayer::Impl SHALL 删除所有单节点成员指针。控制操作 SHALL 通过 graph 高层操作实现。

#### Scenario: MediaPlayer Seek 不触碰节点
- **WHEN** MediaPlayer::Seek(t) 被调用
- **THEN** 仅调用 graph_->Seek(t)，不直接调用任何节点方法，也不直接重置时钟（时钟重置由 graph 内部广播完成）
