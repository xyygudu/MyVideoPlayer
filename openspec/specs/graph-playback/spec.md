## Purpose

Defines the MediaPlayer class that serves as the preset playback graph builder,
replacing the old Player API. MediaPlayer constructs and manages the playback
graph topology for common playback scenarios.

## Requirements

### Requirement: MediaPlayer builds playback graph from URL
系统 SHALL 定义 `MediaPlayer` 类，作为播放场景的预设图构建器，替代旧 `Player` 公共 API。

MediaPlayer SHALL 提供：
- `Open(const std::string& url)`：构建播放图
- `Play()`：调用 MediaGraph::Start()
- `Pause()`：暂停 Clock + 暂停节点
- `Seek(double seconds)`：调用 MediaGraph::Flush() + DemuxNode seek
- `SetVideoCallback(VideoFrameCallback)` / `SetAudioCallback(AudioFrameCallback)`
- `Graph()`：返回底层 MediaGraph 引用

#### Scenario: Open builds correct graph topology
- **WHEN** 打开包含音视频流的 MP4 文件
- **THEN** 图拓扑为 DemuxNode（Source）分出 2 个 DecoderNode，分别连到 VideoSinkNode 和 AudioSinkNode

#### Scenario: Open pure video file (no audio)
- **WHEN** 打开无音频流的文件
- **THEN** 仅创建视频 DecoderNode→VideoSinkNode，SyncMode 自动设为 VideoMaster

#### Scenario: Play/Pause/Seek delegates to graph
- **WHEN** 调用 Play() / Pause() / Seek(30.0)
- **THEN** 转换为 MediaGraph::Start() / 暂停 / Flush() + seek

### Requirement: MediaPlayer exposes clock position
MediaPlayer SHALL 提供 `CurrentPosition()` / `Duration()` 方法。

#### Scenario: Track playback progress
- **WHEN** 播放到 15 秒
- **THEN** CurrentPosition() 返回约 15.0
