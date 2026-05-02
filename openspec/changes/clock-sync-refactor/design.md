## Context

当前 `mvp` 播放器核心层由 Demuxer → PacketQueue → Decoder → FrameQueue → Renderer 的线程管线组成，参照 FFplay 架构。但在以下方面偏离了业界成熟做法：

- Clock 仅存储单一 PTS 值，无 wall-time 关联，导致无音频时视频无法自驱动
- 状态管理使用多个 `atomic<bool>` 组合，`step_one_frame_` 作为补丁横切
- 队列的 `FlushAndIncrementSerial()` 混合了数据清理和生命周期管理
- 音频 Decoder 被 AudioOutput 内部持有，与视频 Decoder 由 Player 直接持有形成不对称

现有 specs 覆盖了 av-sync、demux-decode、playback-control、frame-position、logging、player-ui。本次重构需保持公共 API (`Player` 类) 向后兼容，仅新增接口。

## Goals / Non-Goals

**Goals:**
- Clock 支持 wall-time 外推，无音频文件可正常播放
- 消除所有补丁式 bool 标志，用正式状态机驱动
- 队列接口语义清晰：数据操作与生命周期管理分离
- 音视频管线结构对称，Seek/EOF 等操作有统一路径
- EOF 能从 Demuxer 传递到 UI
- 所有 magic number 替换为命名常量
- Seek 后实现帧精确定位

**Non-Goals:**
- 运行时切换 master clock（不支持）
- 动态自适应同步阈值（FFplay 的高级特性，V1 用静态阈值）
- 多音轨/多视频轨切换
- 硬件加速解码
- 网络流/直播支持
- 播放速率变速功能（Clock 预留 speed 字段，但 UI 和音频 pitch 不在本次范围）

## Decisions

### 1. Clock 线程安全方案：SeqLock

**选择**：SeqLock（序列锁）

**替代方案**：
| 方案 | 优点 | 缺点 |
|------|------|------|
| std::mutex | 简单 | Get() 每帧调用，锁竞争影响性能 |
| 多个 atomic | 无锁 | 多字段不原子，读到不一致组合 |
| CAS + packed 128-bit | 真正 lock-free | 平台依赖，double 精度受限 |
| **SeqLock** | 读端无锁无阻塞，写端轻量 | 实现稍复杂，需要注释说明 |

**理由**：Clock 是典型的"单写多读"场景（一个 renderer 线程写，多个线程读），SeqLock 是 Linux 内核 `struct timekeeper` 和 mpv 的标准做法。代码中关键位置加充分注释。

**实现要点**：
- `seq_` 为 `std::atomic<uint32_t>`，奇数表示写入中
- 写端：seq+1 (变奇)→ 写数据 → seq+1 (变偶)
- 读端：循环读取直到前后 seq 一致且为偶数
- 使用 `std::atomic_thread_fence` 保证内存序

### 2. 状态机：单一枚举变量

**选择**：`enum class PlayerState { Idle, Ready, Playing, Paused, Finished }`

**替代方案**：
- 保留 bool 组合 + 文档约束 → 脆弱，组合爆炸
- 完整状态模式（GoF State Pattern）→ 过度工程化

**理由**：5 个状态，转换逻辑不复杂，一个枚举 + TransitionTo() 方法足够。用 `atomic<PlayerState>` 保证线程安全读取。StepFrame 是 Paused 状态下的动作，不是独立状态。

### 3. 队列接口分离

**选择**：拆分为 `Flush()` / `Abort()` / `Reset()` 三个方法

**设计**：
- `Flush()`：清空数据 + 递增 serial。不改变 abort 标志。用于 Seek。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。用于 Stop/Close。
- `Reset()`：abort=false + serial=0。用于重新启动（Close 后再 Open 同一队列对象）。

### 4. StreamContext：对称封装

**选择**：`struct StreamContext`，聚合 PacketQueue + Decoder + FrameQueue

**理由**：
- 音视频共享相同的数据流拓扑，应对称管理
- `Flush()` / `Abort()` / `Start()` / `Stop()` 在 StreamContext 层统一调用，避免遗漏
- AudioRenderer 从外部消费 audio StreamContext 的 frame_queue，与 VideoRenderLoop 消费 video frame_queue 对称

### 5. EOF 传递机制

**选择**：null packet → drain → EOF frame marker → callback

**流程**：
1. Demuxer 读到 AVERROR_EOF → 向每个 PacketQueue 推入一个 `data=NULL, size=0` 的 flush packet
2. Decoder 收到 null packet → `avcodec_send_packet(ctx, NULL)` 进入 drain → 输出剩余帧 → 推入 EOF marker（`frame=nullptr, eof=true`）
3. Renderer 收到 EOF marker → 通知 Player → TransitionTo(Finished) → 触发回调

**Seek 时**：Flush 清除队列中的 EOF marker，Demuxer seek 后恢复正常推包。

### 6. Master Clock 策略

**选择**：Open 时根据流情况确定，运行期不变

```
有音频流 → SyncMode::AudioMaster → audio_clock_ 驱动
无音频流 → SyncMode::VideoMaster → video_clock_ 自驱动
```

VideoMaster 模式下，帧间隔优先用 `current_pts - last_pts`，异常时 fallback 到 `1.0 / fps`。

### 7. 同步常量管理

**选择**：单独的 `sync_constants.h`，使用 `inline constexpr`

```cpp
namespace mvp::sync {
inline constexpr double kSyncThreshold = 0.04;    // 40ms，约 1 帧 @25fps
inline constexpr double kDropThreshold = 0.1;     // 100ms，超过则丢帧
inline constexpr double kMaxSleepSeconds = 0.1;   // 避免 seek 后长时间卡住
inline constexpr double kFrameDelayMin = 0.001;   // 最小合法帧间隔
inline constexpr double kFrameDelayMax = 1.0;     // 最大合法帧间隔
inline constexpr int kDefaultVideoQueueSize = 3;
inline constexpr int kDefaultAudioQueueSize = 9;
inline constexpr int64_t kDefaultMaxQueueBytes = 15 * 1024 * 1024;
}
```

### 8. 帧精确 Seek

**选择**：在 VideoRenderLoop 中用 `seek_target_` 丢弃 PTS < target 的帧

**理由**：
- 不需要修改 Decoder 逻辑
- 音频侧允许不精确（人耳不敏感）
- Seek target 用 `atomic<double>` 存储，-1.0 表示无 seek 进行中

## Risks / Trade-offs

**[SeqLock 在 MSVC/ARM 上的正确性]** → 使用 `std::atomic_thread_fence(memory_order_acquire/release)` 而非编译器 barrier，保证跨平台正确。加单元测试验证。

**[状态转换竞态]** → TransitionTo() 使用 compare_exchange_strong，只允许合法转换（如只能从 Playing/Paused 转到 Finished）。非法转换记日志并忽略。

**[StreamContext 增加了间接层]** → 代码量略增，但换来 Seek/Abort/Stop 的单一调用点，长期维护收益明显。

**[EOF marker 增加 FrameQueue 复杂度]** → SerialFrame 增加 `bool eof` 字段，Pop 返回时调用者检查。影响面小。

**[帧精确 Seek 增加首帧延迟]** → Seek 后需要解码从关键帧到目标帧之间的所有帧。对于 GOP 很长的文件（如 10s），可能延迟明显。可接受，后续优化方向是在 Decoder 层做 discard（不做色彩空间转换）。
