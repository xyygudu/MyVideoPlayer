## Purpose

Defines the Command event mechanism for high-level control intent (seek, pause)
propagated through the graph. Commands express intent without prescribing
mechanism; each node decides how to respond.
## Requirements
### Requirement: Command 事件机制
系统 SHALL 定义 `Command` 结构体和 `CommandType` 枚举，作为图中高层控制意图的载体。

```cpp
enum class CommandType { kSeek, kResize, kEndOfStream };
struct Command {
    CommandType type;
    double position{0.0};  // kSeek
    int width{0};          // kResize
    int height{0};         // kResize
};
```

Command SHALL 只表达高层控制意图，不表达机制步骤。flush、drop_until_pts、渲染器重建等机制 SHALL 由节点响应命令时内部处理。参数字段按命令类型使用。`kEndOfStream` 表示"实时源结束"：实时源停止采集并向下游推送 EOS，其余节点忽略。

#### Scenario: 加新命令不改接口
- **WHEN** 未来需要支持暂停步进
- **THEN** 仅在 CommandType 枚举增加新值，INode::OnCommand 签名不变

#### Scenario: 不关心的节点忽略新意图
- **WHEN** 广播 {kResize} 到全图
- **THEN** DemuxNode / DecoderNode / AudioSinkNode 等无显示概念的节点默认无动作

#### Scenario: 实时源响应结束意图
- **WHEN** Recorder 广播 {kEndOfStream}
- **THEN** 屏幕、音频、摄像头采集节点各推送一次 EOS；EncoderNode、MuxNode 不处理该命令，而是在收到 EOS 后冲刷、写尾

### Requirement: kResize 表达窗口尺寸变化的意图
系统 SHALL 定义 `CommandType::kResize`，携带新的宽高，表示"输出窗口尺寸已改变"。

facade SHALL 通过 graph 广播该意图，SHALL NOT 直接调用渲染器的尺寸接口 —— 渲染器由持有渲染线程的节点独占修改。同一个用户动作 SHALL 只有这一条路径。

#### Scenario: facade 不直接操作渲染器
- **WHEN** MediaPlayer 处理窗口尺寸变化
- **THEN** 仅广播 {kResize, w, h}，不调用 `VideoRenderer::Resize`，也不引用任何具体节点类型

#### Scenario: 暂停态窗口缩放立即重新布局
- **WHEN** 播放暂停时拖拽窗口改变尺寸
- **THEN** VideoSinkNode 应用新尺寸并重绘当前帧，无需恢复播放

#### Scenario: 图未建立时尺寸不丢失
- **WHEN** 尚未打开文件时窗口被缩放
- **THEN** 命令无接收者，但 facade 仍记录尺寸，供后续渲染器初始化使用

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

