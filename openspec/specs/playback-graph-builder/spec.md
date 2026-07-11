## Purpose

Defines how MediaPlayer::BuildGraph constructs a playback graph directly
without a separate Builder class. Inline Connect calls keep pipeline wiring
flexible for future filter insertion.

## Requirements

### Requirement: PlaybackGraphBuilder 封装播放图构建
系统 SHALL 不引入独立的 `PlaybackGraphBuilder` 类或 `PlaybackContext` 结构体。

`MediaPlayer::Impl::BuildGraph` SHALL 作为私有方法直接实现播放图构建，通过预探帧消除数据依赖后函数体 ≤ 40 行。构建阶段为：创建 Graph → 创建节点（含 Setter 配置）→ inline Connect 连线 → Negotiate + Prepare。

#### Scenario: BuildGraph 职责单一
- **WHEN** MediaPlayer::BuildGraph 重构后
- **THEN** 函数仅包含：new MediaGraph → AddNode（含 Setter 配置）→ Connect → Negotiate/Prepare
- **AND** 无指针森林，无分散的元数据提取
