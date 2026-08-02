## MODIFIED Requirements

### Requirement: MediaGraph broadcasts pause and seek to all clocks
`SetPaused(bool)` SHALL 在暂停/恢复所有节点后，将同一状态广播给图内全部时钟。
`Seek(double)` SHALL 依次执行：`Flush()` → 广播 seek 命令 → 将全部时钟重置到目标位置。seek 世代的递增由 `Flush()` 承担，`Seek()` SHALL NOT 自行递增。

时钟的暂停与重置 SHALL 只有这一条路径，facade SHALL NOT 直接持有或操作时钟对象。

#### Scenario: Pause freezes every clock
- **WHEN** 调用 MediaGraph::SetPaused(true)
- **THEN** 所有节点进入暂停且所有时钟冻结，`MasterClock()->Get()` 不再随墙钟推进

#### Scenario: Seek repositions every clock
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** 世代递增、Link 清空、节点收到 {kSeek, 30.0}、所有时钟重置为 30.0

## ADDED Requirements

### Requirement: Flush 自持世代不变量
`MediaGraph::Flush()` SHALL 在清空 Link **之前**递增 seek 世代，使「清空队列」与「作废在途数据」成为不可分割的单一操作。

理由：清空队列会唤醒阻塞在 `Push` 上的生产者，它们随即把手中的旧数据入队。若世代递增交由调用方在 `Flush()` 前后自行安排，该不变量就依赖调用方记得配对 —— 单独调用 `Flush()` 将清空队列却不作废在途数据，导致陈旧数据被当作当代数据放行，且无任何报错。

`Flush()` SHALL 因此是独立可安全调用的操作，不要求调用方补做任何配套步骤。

#### Scenario: 单独调用 Flush 亦作废在途数据
- **WHEN** 直接调用 `MediaGraph::Flush()`（不经由 Seek）
- **THEN** 世代递增，此后抵达的 pre-flush 在途数据仍被端口校验丢弃

#### Scenario: 递增先于清空
- **WHEN** Flush 唤醒某个阻塞在 Push 上的生产者，它立即把手中的旧 buffer 入队
- **THEN** 消费者此时读到的已是新世代，该 buffer 被判定为过期
