## MODIFIED Requirements

### Requirement: Clock is managed by MediaGraph
Clock 实例 SHALL 从 PlayerImpl 内部成员提升为 MediaGraph 的全局资源。MediaGraph SHALL 通过 `SetClock(shared_ptr<IClock>)` 接受外部注入的 Clock，或默认创建内部的 Clock。

SyncMode（AudioMaster/VideoMaster）SHALL 保留，由 MediaPlayer 在构建图时确定并设置。Sink 节点（VideoSinkNode/AudioSinkNode）SHALL 通过 MediaGraph 获取 Clock 引用。

转码等非实时场景 SHALL 不注入 Clock，节点全速处理。

#### Scenario: AudioMaster clock reference
- **WHEN** 打开含音频流的文件
- **THEN** SyncMode 设为 AudioMaster，AudioSinkNode 更新 audio_clock，该 clock 作为 MasterClock

#### Scenario: VideoSinkNode references graph clock
- **WHEN** VideoSinkNode 需要计算同步延迟
- **THEN** 通过 Graph().Clock() 获取 Clock 引用，读取当前时间

#### Scenario: No clock in transcode mode
- **WHEN** Transcoder 构建图
- **THEN** Graph 不注入 Clock，节点全速处理无同步等待

### Requirement: AudioMaster sync uses frame_timer accumulation
AudioMaster 模式下的视频帧同步逻辑 SHALL 从 PlayerImpl 迁移到 VideoSinkNode，frame_timer 累积算法保留。

#### Scenario: Video frame syncs to audio clock
- **WHEN** AudioMaster 模式，video_pts 与 audio_clock 差值在容差内
- **THEN** 帧按正常间隔显示

### Requirement: VideoMaster mode uses frame-interval timing
VideoMaster 模式 SHALL 保留，由 VideoSinkNode 基于帧间隔自驱动。

#### Scenario: Video self-drives in VideoMaster
- **WHEN** 无音频流的文件播放
- **THEN** VideoSinkNode 使用帧间隔驱动显示，不参考 audio_clock
