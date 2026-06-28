## Requirements

### Requirement: SyncMode determines synchronization strategy
系统 SHALL 定义 `enum class SyncMode { AudioMaster, VideoMaster }`。Open 时根据流情况确定：有音频流时为 AudioMaster，无音频流时为 VideoMaster。运行期不可切换。

### Requirement: Clock is managed by MediaGraph
Clock 实例 SHALL 从 PlayerImpl 内部成员提升为 MediaGraph 的全局资源。Sink 节点 SHALL 通过 MediaGraph 获取 Clock 引用。转码等非实时场景 SHALL 不注入 Clock。

#### Scenario: AudioMaster clock reference
- **WHEN** 打开含音频流的文件
- **THEN** SyncMode 设为 AudioMaster，AudioSinkNode 更新 audio_clock 作为 MasterClock

### Requirement: AudioMaster sync uses frame_timer accumulation
AudioMaster 模式下的视频帧同步逻辑 SHALL 从 PlayerImpl 迁移到 VideoSinkNode，frame_timer 累积算法保留。

### Requirement: VideoMaster mode uses frame-interval timing
在 VideoMaster 模式下，VideoSinkNode SHALL 基于帧间隔和系统时钟自驱动视频显示节奏。
