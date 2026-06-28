## Requirements

### Requirement: Play starts graph execution
Play() 操作 SHALL 通过 MediaGraph::Start() 级联启动所有节点。

### Requirement: Pause pauses graph execution
Pause() 操作 SHALL 通过冻结 Clock 实现，GraphState 转为 Paused。

### Requirement: Seek flushes graph and repositions
Seek() 操作 SHALL 调用 MediaGraph::Flush() + SendCommand(kSeek)。

### Requirement: Duration and position queries
系统 SHALL 提供 Duration() / CurrentPosition() / VideoFps() / State() 查询接口。

### Requirement: Close releases resources
Close() SHALL 调用 MediaGraph::Stop() 释放所有资源。

### Requirement: StepFrame advances one frame while paused
StepFrame() 仅在 Paused 状态下有效。

### Requirement: Playback finished callback
SetPlaybackFinishedCallback 在状态转为 Finished 时触发。
