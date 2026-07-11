## MODIFIED Requirements

### Requirement: PlaybackGraphBuilder 封装播放图构建
系统 SHALL 不引入独立的 `PlaybackGraphBuilder` 类或 `PlaybackContext` 结构体。

`MediaPlayer::Impl::BuildGraph` SHALL 作为私有方法直接实现播放图构建，通过预探帧消除数据依赖后函数体 ≤ 40 行。构建阶段为：探帧 → 建图 → 创建节点（含 Setter 配置）→ inline Connect 连线 → Negotiate + Prepare。

#### Scenario: BuildGraph 职责单一
- **WHEN** MediaPlayer::BuildGraph 重构后
- **THEN** 函数仅包含：SourceProbe::Probe → new MediaGraph → AddNode（含 Setter 配置）→ Connect → Negotiate/Prepare
- **AND** 无指针森林（demux_node / video_decoder_node / ...），无分散的元数据提取

## REMOVED Requirements

### Requirement: 滤镜就绪的链式管线构建
**Reason**: `AddVideoPipeline(stream, filters)` / `AddAudioPipeline(...)` 封装过度。连线保持 inline 更灵活——未来加滤镜链只需在 Decoder→Sink 之间插入 Connect 调用，不改变任何函数签名。
**Migration**: BuildGraph 中 DEC 到 Sink 的连线保持 inline `graph_->Connect(...)`，加滤镜时在该位置插入额外 Connect 调用。

### Requirement: PlaybackContext 依赖注入
**Reason**: `PlaybackContext` 结构体打包 6+ 个依赖字段，仅服务于单一函数调用，引入不必要的间接层。
**Migration**: `BuildGraph` 直接访问 `MediaPlayer::Impl` 的成员（`video_renderer_`、`audio_clock_` 等），无需中间结构体。
