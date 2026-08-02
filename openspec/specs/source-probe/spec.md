## Purpose

Defines the SourceProbe utility for pre-graph source inspection, returning
SourceInfo. Probe is independent of Graph lifecycle — it opens, reads
metadata, and closes the file as a pure function.

## Requirements

### Requirement: ISourceNode 探测接口
系统 SHALL 定义 `SourceProbe` 工具类，提供静态方法 `static SourceInfo Probe(const std::string& filepath)`。

Probe SHALL 独立于 Graph 生命周期：打开文件 → `avformat_find_stream_info` → 填充 SourceInfo → 关闭文件。Probe SHALL 不持有任何 Graph Node 引用，不依赖 `AVFormatContext` 的长生命周期。

```cpp
class SourceProbe {
public:
    static SourceInfo Probe(const std::string& filepath);
};
```

Probe 失败时 SHALL 返回空的 SourceInfo（`video_streams` 和 `audio_streams` 均为空），由调用方判空。

#### Scenario: 成功探测含视频的文件
- **WHEN** `SourceProbe::Probe("sample.mp4")` 在含 1 个视频流和 1 个音频流的文件上调用
- **THEN** 返回的 `SourceInfo` 中 `video_streams.size() == 1`，`audio_streams.size() == 1`
- **AND** 各流的 `codec_name`、分辨率、帧率、采样率等信息正确

#### Scenario: 探测失败返回空结构
- **WHEN** `SourceProbe::Probe("nonexistent.mp4")` 在不存在或损坏的文件上调用
- **THEN** 返回的 `SourceInfo` 中 `video_streams` 和 `audio_streams` 均为空

### Requirement: 探测独立于图统一生命周期
源探测 SHALL 作为完全独立于 Graph 的前置阶段。MediaPlayer::Open SHALL 先调用 `SourceProbe::Probe()` 获取 `SourceInfo` 并缓存为成员 `info_`，选择默认流索引（第一个视频流、第一个音频流），然后调用 `BuildGraph()` 构建拓扑，最后图统一 Open→Negotiate→Prepare。

由于图为静态连线（`Connect()` 早于所有生命周期阶段，端口数量在建图时固定），建图前探测 SHALL 保留：不先知道文件含哪些流就无法决定创建几个端口。

DemuxNode SHALL 在 `Open()` 中打开文件（`avformat_open_input`），`Negotiate()` SHALL NOT 打开文件或分配资源。`OpenFile()` 保留幂等守卫以支持多次调用。

#### Scenario: 探测先于建图
- **WHEN** MediaPlayer::Open(path)
- **THEN** 顺序为：SourceProbe::Probe(path) → 选择流索引 → BuildGraph() → graph->Open() → graph->Negotiate() → graph->Prepare()

#### Scenario: DemuxNode 边界校验
- **WHEN** Negotiate 中 video_stream_index >= nb_streams
- **THEN** 记录 ERROR 日志并返回 false（防止越界访问）

### Requirement: MediaPlayer 缓存 SourceInfo 和流索引
MediaPlayer SHALL 将 `SourceProbe::Probe()` 的返回值缓存为成员 `info_`，并将选择的流索引缓存为 `video_stream_index_` / `audio_stream_index_`。

Duration() SHALL 返回 `info_.duration`。CurrentPosition() SHALL 根据 `audio_stream_index_ >= 0` 选择 audio_clock 或 video_clock。VideoFps() SHALL 从 `info_.video_streams[0]` 按需计算。

#### Scenario: Duration 读缓存不触碰节点
- **WHEN** MediaPlayer::Duration() 被调用
- **THEN** 返回 `info_.duration`，不访问任何节点

#### Scenario: 无音频时 CurrentPosition 用视频时钟
- **WHEN** audio_stream_index_ < 0
- **THEN** CurrentPosition() 返回 video_clock_.Get()
