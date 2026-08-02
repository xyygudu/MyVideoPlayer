## MODIFIED Requirements

### Requirement: MediaPlayer builds playback graph from URL
系统 SHALL 定义 `MediaPlayer` 类，作为播放场景的预设图构建器，替代旧 `Player` 公共 API。

MediaPlayer SHALL 提供：
- `Open(const std::string& url)`：构建播放图
- `Play()`：调用 MediaGraph::Start()
- `Pause()`：调用 MediaGraph::SetPaused(true)（节点与时钟由 graph 统一冻结）
- `Seek(double seconds)`：调用 MediaGraph::Seek()
- `SetVideoCallback(VideoFrameCallback)` / `SetAudioCallback(AudioFrameCallback)`
- `Graph()`：返回底层 MediaGraph 引用

MediaPlayer SHALL NOT 持有 Clock 实例，也 SHALL NOT 在建图时注入时钟或同步策略。

#### Scenario: Open builds correct graph topology
- **WHEN** 打开包含音视频流的 MP4 文件
- **THEN** 图拓扑为 DemuxNode（Source）分出 2 个 DecoderNode，分别连到 VideoSinkNode 和 AudioSinkNode

#### Scenario: Open pure video file (no audio)
- **WHEN** 打开无音频流的文件
- **THEN** 仅创建视频 DecoderNode→VideoSinkNode，主时钟仲裁后落在 VideoSinkNode 自身，其进入自由走时

#### Scenario: Play/Pause/Seek delegates to graph
- **WHEN** 调用 Play() / Pause() / Seek(30.0)
- **THEN** 转换为 MediaGraph::Start() / SetPaused(true) / Seek(30.0)，不直接操作任何时钟

### Requirement: MediaPlayer exposes clock position
MediaPlayer SHALL 提供 `CurrentPosition()` / `Duration()` 方法。`CurrentPosition()` SHALL 读取 `MediaGraph::MasterClock()`。

#### Scenario: Track playback progress
- **WHEN** 播放到 15 秒
- **THEN** CurrentPosition() 返回约 15.0
