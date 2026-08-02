## MODIFIED Requirements

### Requirement: MediaGraph broadcasts pause and seek to all clocks
`SetPaused(bool)` SHALL 在暂停/恢复所有节点后，将同一状态广播给图内全部时钟。
`Seek(double)` SHALL 依次执行：递增 seek 世代 → Flush 全部 Link → 广播 seek 命令 → 将全部时钟重置到目标位置。

时钟的暂停与重置 SHALL 只有这一条路径，facade SHALL NOT 直接持有或操作时钟对象。

#### Scenario: Pause freezes every clock
- **WHEN** 调用 MediaGraph::SetPaused(true)
- **THEN** 所有节点进入暂停且所有时钟冻结，`MasterClock()->Get()` 不再随墙钟推进

#### Scenario: Seek repositions every clock
- **WHEN** 调用 MediaGraph::Seek(30.0)
- **THEN** 世代递增、Link 清空、节点收到 {kSeek, 30.0}、所有时钟重置为 30.0

## ADDED Requirements

### Requirement: MediaGraph owns the seek epoch
`MediaGraph` SHALL 持有 `seek_epoch_` 并提供只读访问 `SeekEpoch()`。该计数器 SHALL 是全图唯一的 seek 世代来源。

`MediaGraph::Connect()` SHALL 在建立连接后为下游输入端口绑定世代来源，使端口无需依赖 `MediaGraph` 类型即可读取当前世代。

#### Scenario: 端口以最小权限访问世代
- **WHEN** InputPort 需要判断 buffer 是否过期
- **THEN** 它只持有指向世代计数器的只读引用，不持有 MediaGraph 指针，`port.h` 不包含 `media_graph.h`

#### Scenario: 未连接端口的世代退化安全
- **WHEN** 某输入端口未经 `MediaGraph::Connect` 建立连接
- **THEN** 其 Pull() 直接返回空，不会因缺少世代来源而误判数据
