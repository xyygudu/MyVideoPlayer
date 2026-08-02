## MODIFIED Requirements

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
