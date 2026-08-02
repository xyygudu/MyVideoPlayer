## MODIFIED Requirements

### Requirement: Command 事件机制
系统 SHALL 定义 `Command` 结构体和 `CommandType` 枚举，作为图中高层控制意图的载体。

```cpp
enum class CommandType { kSeek, kResize };
struct Command {
    CommandType type;
    double position{0.0};  // kSeek
    int width{0};          // kResize
    int height{0};         // kResize
};
```

Command SHALL 只表达高层控制意图，不表达机制步骤。flush、drop_until_pts、渲染器重建等机制 SHALL 由节点响应命令时内部处理。参数字段按命令类型使用。

#### Scenario: 加新命令不改接口
- **WHEN** 未来需要支持暂停步进
- **THEN** 仅在 CommandType 枚举增加新值，INode::OnCommand 签名不变

#### Scenario: 不关心的节点忽略新意图
- **WHEN** 广播 {kResize} 到全图
- **THEN** DemuxNode / DecoderNode / AudioSinkNode 等无显示概念的节点默认无动作

## REMOVED Requirements

### Requirement: kRedraw 表达重新呈现当前画面的意图
**Reason**: 重绘不是一个独立的用户意图 —— 当前唯一的触发源就是窗口尺寸变化，而尺寸变化还需要把新尺寸带给渲染器。拆成两步（UI 线程直接改尺寸 + 广播重绘）正是跨线程竞态的成因。
**Migration**: 由 `kResize 表达窗口尺寸变化的意图` 取代，重绘成为尺寸应用后的必然后续。

## ADDED Requirements

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
