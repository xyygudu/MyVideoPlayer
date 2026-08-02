## MODIFIED Requirements

### Requirement: MediaPlayer 缓存 SourceInfo 和流索引
MediaPlayer SHALL 将 `SourceProbe::Probe()` 的返回值缓存为成员 `info_`，并将选择的流索引缓存为 `video_stream_index_` / `audio_stream_index_`。

Duration() SHALL 返回 `info_.duration`。CurrentPosition() SHALL 查询 `graph_->MasterClock()`，SHALL NOT 依据流索引自行选择时钟——主时钟的选择只在 MediaGraph 仲裁期发生一次。VideoFps() SHALL 从 `info_.video_streams[0]` 按需计算。

#### Scenario: Duration 读缓存不触碰节点
- **WHEN** MediaPlayer::Duration() 被调用
- **THEN** 返回 `info_.duration`，不访问任何节点

#### Scenario: 无音频时 CurrentPosition 仍走主时钟
- **WHEN** audio_stream_index_ < 0
- **THEN** CurrentPosition() 返回 `MasterClock()->Get()`（此时主时钟为视频 sink 提供）

#### Scenario: 未打开文件时 CurrentPosition 安全返回
- **WHEN** graph 尚未构建或无任何时钟提供者
- **THEN** CurrentPosition() 返回 0.0，不解引用空指针
