## REMOVED Requirements

### Requirement: SyncMode determines synchronization strategy
**Reason**: 主时钟不再由枚举硬编码。"有无音频"这一条件已隐含在"是否存在音频时钟提供者"中，由 `MediaGraph` 按 `ClockOffer` 优先级仲裁，判据只存在一处。
**Migration**: 由 `Master clock is arbitrated by MediaGraph` 取代。

### Requirement: Clock is managed by MediaGraph
**Reason**: 时钟由 graph 持有再下发，与"写入者才是拥有者"矛盾——真正写时钟的是 sink 节点。改为节点自持、graph 仲裁并共享引用。
**Migration**: 由 `Master clock is arbitrated by MediaGraph` 取代。

### Requirement: AudioMaster sync uses frame_timer accumulation
**Reason**: 策略选择的判据由"音频主/视频主"改为"有无外部参考时基"。
**Migration**: 由 `Video sink syncs against external reference or free-runs` 取代。

### Requirement: VideoMaster mode uses frame-interval timing
**Reason**: 同上。
**Migration**: 由 `Video sink syncs against external reference or free-runs` 取代。

## ADDED Requirements

### Requirement: Master clock is arbitrated by MediaGraph
时钟 SHALL 由写入它的节点持有，而非由 facade 持有再注入节点。

`INode::ProvideClock()` SHALL 返回 `ClockOffer{clock, priority}`；不提供时基的节点返回空 offer。`MediaGraph` SHALL 在协商期收集全部 offer，取优先级最高者作为主时钟（优先级相同时按拓扑序取先者），并 SHALL NOT 依据节点类型或名称推断适任度。

转码等非实时场景无任何时钟提供者，`MasterClock()` SHALL 返回 nullptr。

#### Scenario: Audio sink wins arbitration
- **WHEN** 图中同时存在 AudioSinkNode 与 VideoSinkNode
- **THEN** AudioSinkNode 的时钟优先级更高，`MasterClock()` 返回其时钟

#### Scenario: Video sink wins when no audio
- **WHEN** 图中只有 VideoSinkNode 提供时钟
- **THEN** `MasterClock()` 返回 VideoSinkNode 自身的时钟

#### Scenario: Transcode graph has no clock
- **WHEN** 转码图（Demux→Decoder→Encoder→Mux）完成协商
- **THEN** `MasterClock()` 返回 nullptr，无节点因缺少时钟而失败

### Requirement: Video sink syncs against external reference or free-runs
`VideoSinkNode` SHALL 在协商期取得 `MediaGraph::MasterClock()`，并据此选择走时策略：
- 主时钟不是自身时钟 → 从钟模式，按 frame_timer 累积算法向主时钟收敛
- 主时钟是自身时钟或为空 → 自由走时，基于帧间隔与系统时钟自驱动显示节奏

判据 SHALL 为"是否存在外部参考时基"，SHALL NOT 引入音频/视频主从的枚举。

#### Scenario: Slaved to audio clock
- **WHEN** 主时钟为 AudioSinkNode 提供的时钟
- **THEN** VideoSinkNode 按 `diff = pts - MasterClock()->Get()` 修正显示延迟

#### Scenario: Free-run when self is master
- **WHEN** `MasterClock()` 与 VideoSinkNode 自身时钟为同一对象
- **THEN** VideoSinkNode 按帧间隔自驱动，不读取任何外部时钟

#### Scenario: External clock takes over without node changes
- **WHEN** 未来接入优先级高于音频的外部时钟提供者
- **THEN** VideoSinkNode 与 AudioSinkNode 自动降为从钟，两个节点的代码无需改动
