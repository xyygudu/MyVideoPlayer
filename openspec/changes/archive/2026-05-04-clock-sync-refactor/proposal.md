## Why

当前 Clock 仅为一个 `atomic<double>`，无法推算两次 Set 之间的时间流逝，导致：无音频时视频播放卡死、进度条阶梯跳变、暂停/恢复时间不连续。此外，多个 `atomic<bool>` 标志位组合代替状态机、FlushAndIncrementSerial 混合数据清理与生命周期管理、音视频 Decoder 所有权不对称、EOF 无传递等问题相互交织，需要一次性以统一的设计思路修正，避免逐个补丁破坏架构一致性。

## What Changes

- **Clock 升级为 wall-time 线性外推器**：基于 SeqLock 实现无锁读，支持 Set/Get/SetPaused/SetSpeed，Get() 返回基于系统时钟外推的当前值
- **引入 SyncMode + Master Clock 概念**：有音频时 AudioMaster，无音频时 VideoMaster；UI 统一从 MasterClock() 读取进度
- **PlayerState 枚举状态机**：替代 `running_` / `paused_` / `step_one_frame_` 的 bool 组合
- **PacketQueue/FrameQueue 职责分离**：`Flush()` 只清数据+递增 serial；`Abort()` 只管生命周期；`Reset()` 恢复初始状态
- **StreamContext 对称封装**：音频和视频各自拥有 `{PacketQueue, Decoder, FrameQueue}` 的对称结构
- **AudioOutput → AudioRenderer 职责收窄**：不再拥有 Decoder 和 FrameQueue，只负责 SDL 输出和重采样
- **EOF 传递链**：Demuxer 发送 null packet → Decoder drain → FrameQueue EOF 标记 → Player 状态转为 Finished + 回调通知
- **帧精确 Seek**：VideoRenderLoop 在 seek 后丢弃 PTS < target 的帧
- **命名常量**：所有同步阈值使用 `constexpr` 命名常量，消除 magic number

## Capabilities

### New Capabilities
- `wall-clock`: 基于 SeqLock 的 wall-time 线性外推时钟，支持暂停/恢复/变速
- `player-state-machine`: PlayerState 枚举 (Idle/Ready/Playing/Paused/Finished) 及原子状态转换
- `eof-propagation`: 从 Demuxer 到 UI 的 EOF 传递链和播放完成回调
- `stream-context`: 音视频对称的 StreamContext 数据管道封装

### Modified Capabilities
- `av-sync`: 引入 SyncMode (AudioMaster/VideoMaster)，VideoMaster 模式下视频基于帧间隔+系统时钟自驱动；同步阈值使用命名常量
- `demux-decode`: PacketQueue/FrameQueue 接口拆分 (Flush/Abort/Reset 分离)；Demuxer 支持 EOF null packet 推送
- `playback-control`: 状态管理改为 PlayerState 枚举驱动；Seek 支持帧精确；新增 StepFrame() 接口；新增 OnPlaybackFinished 回调

## Impact

- **核心源文件变更**：`clock.h/cc`, `packet_queue.h/cc`, `frame_queue.h/cc`, `decoder.h/cc`, `demuxer.h/cc`, `audio_output.h/cc` → `audio_renderer.h/cc`, `player.cc`, `player.h`
- **新增文件**：`stream_context.h`, `sync_constants.h` (或 `sync_config.h`)
- **公共 API 变更**：`Player` 新增 `StepFrame()`、`SetPlaybackFinishedCallback()`、`State()` 方法
- **依赖**：无新增外部依赖，仅重组内部结构
- **BREAKING**：`AudioOutput` 重命名为 `AudioRenderer`，内部 API 变更（不影响外部公共接口）
