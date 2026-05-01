## Context

当前 Seek 实现依赖三层 Flush + 原子 flag 协调：
1. Player::Seek() 显式 Flush 三个 queue
2. Demuxer::DemuxLoop 检测到 seek_requested_ 后再次 Flush packet queue
3. Decoder::DecodeLoop 检测到 flush_requested_ 后 flush codec + Flush frame_queue
4. Render 线程通过 flush_completed_ 等待 decoder 完成后再 pop

这导致 flush 责任分散、竞态分析困难。FFplay 使用 serial 机制优雅解决此问题。

**约束**：
- Google C++ Style，4 空格缩进，列宽 100
- 函数体不超过 40 行（Google Style 建议）
- 仅在关键位置添加注释，避免过度注释

## Goals / Non-Goals

**Goals:**
- 用 serial 机制替代显式 Flush + flag，让过期数据自然丢弃
- 消除 Player/Demuxer/Decoder 三处分散的 Flush 调用
- 移除 flush_requested_ / flush_completed_ 等等待机制
- 保持外部行为完全不变（seek 结果、暂停 seek 显示帧）
- 函数体符合 Google Style 长度限制

**Non-Goals:**
- 不改变 PacketQueue 的字节限流策略
- 不改变 FrameQueue 的帧数限制
- 不改变 A/V sync 逻辑
- 不改变公共 API

## Decisions

### 1. Serial 存储位置：Queue 内部

**选择：** PacketQueue 和 FrameQueue 各自维护一个 `std::atomic<int> serial_{0}`

**备选：** Player 统一持有 serial 传入各处 → 耦合度高，需要把 serial 作为参数穿透所有层

**理由：** Queue 自包含 serial，职责清晰。Push 时自动打上当前 serial，外部只需调 `FlushAndIncrementSerial()`。

### 2. Packet/Frame 携带 serial 的方式

**选择：** 
- PacketQueue 内部用包装结构 `struct SerialPacket { AVPacket* pkt; int serial; }`
- FrameQueue 内部用包装结构 `struct SerialFrame { AVFrame* frame; int serial; }`
- Pop 接口增加 `int* serial` out 参数

**备选：** 利用 AVPacket/AVFrame 的 opaque 字段 → 语义不清，可能和 FFmpeg 内部冲突

**理由：** 包装结构最清晰，不侵入 FFmpeg 数据结构。

### 3. Decoder flush 触发方式

**选择：** Decoder 记录 `last_serial_`，pop 出 packet 后发现 serial 变化时执行 `avcodec_flush_buffers`

**备选：** 保留 flush_requested_ flag → 违背重构目标

**理由：** Serial 跳变天然代表"seek 发生了"，无需额外信号。这正是 FFplay 的做法。

### 4. step_one_frame_ 保留

**选择：** 保留 `step_one_frame_` 标志用于暂停 seek 渲染

**理由：** Serial 机制解决的是"丢弃旧数据"问题，不解决"暂停时唤醒 render 线程"问题。这两个是正交关注点。但移除 `flush_completed_` 等待——serial 机制保证 pop 出的帧一定是新的。

### 5. Player::Seek() 简化（FFplay 双保险模式）

**选择：** Seek 做：
```
audio_packet_queue_.FlushAndIncrementSerial();
video_packet_queue_.FlushAndIncrementSerial();
video_frame_queue_.FlushAndIncrementSerial();
audio_output_->FlushFrameQueue();  // 内部同样 FlushAndIncrementSerial
demuxer_.RequestSeek(position_seconds);
audio_clock_.Set(position_seconds);
if (paused_) step_one_frame_ = true;
```

**移除：** RequestFlush() 调用、flush_requested_/flush_completed_ 等待
**保留：** Flush 操作（但与 serial++ 合并为原子操作，对齐 FFplay `packet_queue_flush`）

**理由：** FFplay 模式 = Flush 主动释放内存 + serial 兜底防止竞态窗口中推入的旧数据。双保险比纯 serial 内存行为更干净，连续快速 seek 不会积压多轮旧数据。

### 6. Demuxer::DemuxLoop seek 处理简化

**选择：** Seek 后只做 `av_seek_frame`，不再在 Demuxer 内部 Flush queue

**理由：** Player::Seek() 已经做了 FlushAndIncrementSerial，Demuxer 无需二次 Flush。即使有竞态窗口中的旧 packet 漏入，decoder 也会按 serial 丢弃。

## Risks / Trade-offs

- [FlushAndIncrementSerial 的原子性] → 在 mutex 锁内同时执行 flush + serial++，保证不会有 Push 插入到 flush 和 serial++ 之间。
- [Pop 接口变更] → 增加 `int* serial` 参数，Decoder 和 Render 需要适配。变更范围可控。
- [audio_frame_queue_ 在 AudioOutput 内部] → 需暴露 FlushFrameQueue() 接口。选择给 AudioOutput 加 `FlushFrameQueue()` 方法。
- [竞态窗口中的旧数据] → Flush 清掉大部分，serial 兜底清掉漏网的。双保险。
