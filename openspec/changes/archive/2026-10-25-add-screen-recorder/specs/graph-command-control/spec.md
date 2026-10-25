## MODIFIED Requirements

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
