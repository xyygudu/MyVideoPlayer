## ADDED Requirements

### Requirement: Link 只负责有界排队
`Link` SHALL 只承担两项职责：按双维度容量约束的阻塞排队，以及 flush / abort 时唤醒等待方。`Link` SHALL NOT 持有 seek 世代号 —— 世代是图级概念，由 `MediaGraph` 持有。

`Link::Flush()` SHALL 清空队列并唤醒两侧等待者，SHALL NOT 修改任何世代状态。

#### Scenario: Flush 不再递增世代
- **WHEN** 调用 Link::Flush()
- **THEN** 队列清空、两侧条件变量被唤醒，Link 内部不存在任何世代计数

#### Scenario: 陈旧数据的判定不依赖 Link
- **WHEN** 消费者判断一个 buffer 是否过期
- **THEN** 依据是图级世代，而非该 buffer 所经过的 Link 的状态
