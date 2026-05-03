## 1. 基础设施：常量与工具

- [x] 1.1 创建 `src/core/src/sync_constants.h`，定义所有同步相关命名常量（kSyncThreshold, kDropThreshold, kMaxSleepSeconds, kFrameDelayMin, kFrameDelayMax, kDefaultVideoQueueSize, kDefaultAudioQueueSize, kDefaultMaxQueueBytes）
- [x] 1.2 创建 `src/core/include/mvp/player_state.h`，定义 `enum class PlayerState { Idle, Ready, Playing, Paused, Finished }` 和 `enum class SyncMode { AudioMaster, VideoMaster }`

## 2. Clock 升级

- [x] 2.1 重写 `clock.h`：添加 SeqLock 成员（seq_ atomic），pts_, last_updated_, speed_, paused_ 字段；声明 Set/Get/SetPaused/SetSpeed/Reset/Now 接口；添加 BeginWrite/EndWrite 私有方法
- [x] 2.2 重写 `clock.cc`：实现 SeqLock 读写逻辑（Get 循环重试、Set/SetPaused/SetSpeed 写端 seq 操作），使用 steady_clock::now；关键逻辑添加详细注释
- [x] 2.3 验证 Clock 单元编译通过并行为正确（手动或简单测试）

## 3. 队列接口分离

- [x] 3.1 修改 `packet_queue.h`：将 `FlushAndIncrementSerial()` 拆分为 `Flush()` / `Abort()` / `Reset()` 三个方法
- [x] 3.2 修改 `packet_queue.cc`：实现分离后的三个方法（Flush 不改 abort、Abort 不清数据、Reset 恢复初始态）
- [x] 3.3 修改 `frame_queue.h`：同样拆分为 `Flush()` / `Abort()` / `Reset()`；SerialFrame 添加 `bool eof{false}` 字段；添加 `PushEof(int serial)` 方法
- [x] 3.4 修改 `frame_queue.cc`：实现分离后的方法和 PushEof
- [x] 3.5 更新所有调用点（player.cc, audio_output.cc 等）使用新接口名

## 4. StreamContext 封装

- [x] 4.1 创建 `src/core/src/stream_context.h`：定义 struct StreamContext，聚合 PacketQueue + Decoder + FrameQueue，提供 Flush/Abort/Start/Stop 方法
- [x] 4.2 创建 `src/core/src/stream_context.cc`：实现各方法的委托调用

## 5. Demuxer EOF 支持

- [x] 5.1 修改 `demuxer.cc` DemuxLoop：在 av_read_frame 返回 AVERROR_EOF 时，向各 PacketQueue 推入 null packet（data=NULL, size=0），而非 sleep 等待
- [x] 5.2 EOF 推送后进入等待状态（等待 seek 请求或 stop），保持与当前行为兼容

## 6. Decoder EOF 与 Drain

- [x] 6.1 修改 `decoder.cc` DecodeLoop：检测 null packet（data==NULL），调用 avcodec_send_packet(ctx, NULL) 进入 drain，循环 receive 输出剩余帧
- [x] 6.2 Drain 完成后调用 frame_queue_->PushEof(last_serial_) 推入 EOF 标记
- [x] 6.3 Decoder 收到 null packet 后结束 decode loop（或等待新 serial 表示 seek）

## 7. AudioRenderer 重构

- [x] 7.1 将 `audio_output.h/cc` 重命名为 `audio_renderer.h/cc`，类名改为 AudioRenderer
- [x] 7.2 从 AudioRenderer 中移除 Decoder 和 FrameQueue 的持有，改为从外部传入 FrameQueue* 和 Clock* 引用
- [x] 7.3 AudioRenderer::Start() 接受 FrameQueue* 参数，内部 AudioLoop 从该 queue 消费
- [x] 7.4 AudioRenderer 检测 EOF marker 后通知 PlayerImpl（通过回调或 atomic flag）
- [x] 7.5 更新 CMakeLists.txt 中的文件名

## 8. PlayerImpl 状态机重构

- [x] 8.1 在 PlayerImpl 中替换 `running_` / `paused_` / `step_one_frame_` 为 `atomic<PlayerState> state_`
- [x] 8.2 实现 TransitionTo() 方法，使用 compare_exchange_strong 只允许合法转换
- [x] 8.3 添加 SyncMode sync_mode_ 成员，Open() 时根据流情况设定
- [x] 8.4 添加 video_clock_ 成员，实现 MasterClock() 返回对应引用
- [x] 8.5 添加 seek_target_ (atomic<double>)，Seek 时设置，VideoRenderLoop 中丢弃 PTS < target 的帧
- [x] 8.6 添加 playback_finished_cb_ 回调成员和 SetPlaybackFinishedCallback() 公共接口
- [x] 8.7 添加 StepFrame() 方法，使用 mutex + condition_variable 通知渲染线程

## 9. PlayerImpl 管线重组

- [x] 9.1 将散落的 PacketQueue/Decoder/FrameQueue 成员替换为 `unique_ptr<StreamContext> audio_ctx_` 和 `video_ctx_`
- [x] 9.2 重写 Open()：创建 StreamContext，设定 SyncMode
- [x] 9.3 重写 Play()：通过 StreamContext::Start() 统一启动
- [x] 9.4 重写 Pause()：冻结 Clock，暂停 AudioRenderer
- [x] 9.5 重写 Seek()：统一调用 StreamContext::Flush()，设 seek_target_，重设 master clock
- [x] 9.6 重写 Close()：统一调用 StreamContext::Abort()，TransitionTo(Idle)

## 10. VideoRenderLoop 重写

- [x] 10.1 实现 WaitIfPaused()：条件变量等待，支持 StepFrame 唤醒和状态变更唤醒
- [x] 10.2 实现 AudioMaster 同步分支：使用 audio_clock_.Get() + 命名常量判定 sleep/render/drop
- [x] 10.3 实现 VideoMaster 自驱动分支：帧间隔计算 + steady_clock 定时 + video_clock_.Set()
- [x] 10.4 实现 EOF 检测：Pop 到 eof marker 后通知 PlayerImpl
- [x] 10.5 实现帧精确 Seek：检查 seek_target_，丢弃 PTS < target 的帧

## 11. 公共 API 更新

- [x] 11.1 在 `mvp/player.h` 中添加 `State()`, `StepFrame()`, `SetPlaybackFinishedCallback()` 声明
- [x] 11.2 在 player.cc 底部添加这些方法的 impl 委托
- [x] 11.3 在 `mvp/player.h` 中 include `mvp/player_state.h`（导出 PlayerState 枚举）

## 12. 编译验证与集成

- [x] 12.1 更新 `src/core/CMakeLists.txt`，添加新文件（stream_context, sync_constants, player_state, audio_renderer）
- [x] 12.2 全量编译通过，无 error/warning
- [ ] 12.3 运行验证：有音频文件正常播放、Seek、Pause/Resume
- [ ] 12.4 运行验证：纯视频文件（无音频）正常播放
- [ ] 12.5 运行验证：播放到 EOF 触发回调、状态转为 Finished
