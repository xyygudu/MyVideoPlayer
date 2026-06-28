## MODIFIED Requirements

### Requirement: Play starts graph execution
Play() 操作 SHALL 从 PlayerImpl 的直接线程启动改为通过 MediaGraph::Start() 级联启动所有节点。

#### Scenario: Start graph from Ready state
- **WHEN** 状态为 Ready，调用 Play()
- **THEN** MediaGraph::Start() 启动所有 Active 节点的线程，GraphState 转为 Playing

#### Scenario: Resume from Paused state
- **WHEN** 状态为 Paused，调用 Play()
- **THEN** Clock 恢复推进，GraphState 转为 Playing

#### Scenario: Play from Finished restarts
- **WHEN** 状态为 Finished，调用 Play()
- **THEN** MediaGraph::Flush() + DemuxNode seek 到 0，GraphState 转为 Playing

### Requirement: Pause pauses graph execution
Pause() 操作 SHALL 通过冻结 Clock 实现，GraphState 转为 Paused。Active 节点线程 SHALL 在检测到 Paused 状态后等待条件变量。

#### Scenario: Pause during playback
- **WHEN** 状态为 Playing，调用 Pause()
- **THEN** Clock 冻结，GraphState 转为 Paused，视频画面冻结

### Requirement: Seek flushes graph and repositions
Seek() 操作 SHALL 调用 MediaGraph::Flush()（清空所有 Link + 递增 serial + 各节点清内部状态），然后 DemuxNode 执行 avformat_seek_file()。

#### Scenario: Seek to valid position
- **WHEN** 调用 Seek(30.0)
- **THEN** 所有 Link 被 Flush，demux 定位到 30 秒附近，新帧携带新 serial 正常解码渲染

#### Scenario: Frame-accurate seek
- **WHEN** Seek(10.0) 后 DecoderNode 输出 PTS=8.0 的帧（旧 serial）
- **THEN** 帧因 serial 不匹配被丢弃，直到新 serial 的帧到达
