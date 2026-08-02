## MODIFIED Requirements

### Requirement: MediaPlayer 不持有单个节点
MediaPlayer::Impl SHALL 删除所有单节点成员指针。控制操作 SHALL 通过 graph 高层操作实现。

#### Scenario: MediaPlayer Seek 不触碰节点
- **WHEN** MediaPlayer::Seek(t) 被调用
- **THEN** 仅调用 graph_->Seek(t)，不直接调用任何节点方法，也不直接重置时钟（时钟重置由 graph 内部广播完成）
