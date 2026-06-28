## ADDED Requirements

### Requirement: MediaPlayer builds playback graph from URL
系统 SHALL 定义 `MediaPlayer` 类，作为播放场景的预设图构建器，替代旧 `Player` 公共 API。

MediaPlayer SHALL 提供：
- `Open(const std::string& url)`：构建播放图 `DemuxNode → [V]DecoderNode → VideoSinkNode` + `[A]DecoderNode → AudioSinkNode`
- `Play()`：调用 MediaGraph::Start()
- `Pause()`：暂停 Clock + 暂停节点
- `Seek(double seconds)`：调用 MediaGraph::Flush() + DemuxNode seek
- `SetFilter(const std::string& desc)`：停止当前图（Stop）→ 重建图拓扑（在 DecoderNode 和 SinkNode 之间插入 AVFilterNode）→ 重新启动（Negotiate+Prepare+Start）
- `SetFilter("")`：停止当前图 → 重建无滤镜图拓扑 → 重新启动
- `SetVideoCallback(VideoFrameCallback)` / `SetAudioCallback(AudioFrameCallback)`
- `Graph()`：返回底层 MediaGraph 引用，供高级用户直接操作

MediaPlayer SHALL 将 MediaGraph 的 GraphEvent 映射为面向用户的回调。

SetFilter 采用 Stop→Rebuild→Start 策略而非运行时动态插入，因为 MediaGraph 采用静态拓扑模型（图在构建时确定）。重建过程带来的短暂中断（<100ms）对用户不可感知。

#### Scenario: Open builds correct graph topology
- **WHEN** 打开包含音视频流的 MP4 文件
- **THEN** 图拓扑为 DemuxNode（Source）分出 2 个 DecoderNode，分别连到 VideoSinkNode 和 AudioSinkNode

#### Scenario: Open pure video file (no audio)
- **WHEN** 打开无音频流的 AVI 文件
- **THEN** 仅创建视频 DecoderNode→VideoSinkNode，SyncMode 自动设为 VideoMaster

#### Scenario: SetFilter rebuilds graph with filter
- **WHEN** 播放中调用 SetFilter("scale=1280:720")
- **THEN** 当前图 Stop，重建图为 DemuxNode→DecoderNode→AVFilterNode("scale=1280:720")→VideoSinkNode，重新 Start，后续帧先缩放再渲染

#### Scenario: SetFilter empty string removes filter
- **WHEN** 当前图包含 AVFilterNode，调用 SetFilter("")
- **THEN** 当前图 Stop，重建图为 DemuxNode→DecoderNode→VideoSinkNode（无滤镜），重新 Start

#### Scenario: Play/Pause/Seek delegates to graph
- **WHEN** 调用 Play() / Pause() / Seek(30.0)
- **THEN** 转换为 MediaGraph::Start() / 暂停 / Flush() + seek

#### Scenario: SetFilter preserves playback position
- **WHEN** 播放到 30 秒时调用 SetFilter("eq=brightness=0.1")
- **THEN** 重建图后 Seek 到 30 秒继续播放，用户感知的中断时间 <100ms

### Requirement: MediaPlayer exposes clock position
MediaPlayer SHALL 提供 `CurrentPosition()` / `Duration()` 方法，分别返回 MasterClock 时间和文件总时长。

#### Scenario: Track playback progress
- **WHEN** 播放到 15 秒
- **THEN** CurrentPosition() 返回约 15.0
