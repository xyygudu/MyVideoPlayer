# 音视频同步与管线架构待改进点

> 记录时间：2026-05-03
> 对标参考：FFplay (FFmpeg), MPV, VLC

---

## 1. AudioMaster 同步策略缺少 frame_timer 累积校正

### 问题

当前 VideoRenderLoop 在视频超前时直接 `sleep(diff)`，每次独立计算等待时间，不维护连续帧之间的绝对时间基准。

### 影响场景

- **高帧率视频 (60fps+)**：每帧 sleep 的系统调度误差（Windows 典型 1-2ms）会逐帧累积，导致画面轻微抖动（judder）
- **变帧率视频 (VFR)**：帧间隔不均匀时，独立 sleep 无法平滑过渡

### 改进建议（参考 FFplay `video_refresh`）

维护一个 `frame_timer_`（绝对时间戳），每次显示帧后：
```cpp
frame_timer_ += delay;          // delay = 目标帧间隔
double actual_sleep = frame_timer_ - Clock::Now();
if (actual_sleep > 0) sleep(actual_sleep);
```
这样即使单帧 sleep 有误差，后续帧会自动补偿，保持长期对齐。

---

## 2. 缺少连续丢帧保护机制

### 问题

当 `diff < -kDropThreshold` 时无条件丢帧，没有上限计数。

### 影响场景

- **音频短暂卡顿后恢复**：audio clock 瞬间跳变，导致连续几十帧被判定为"过期"全部丢弃，画面长时间冻结（可达数秒）
- **低性能设备解码慢**：解码跟不上实时，持续丢帧导致用户只能看到跳跃画面

### 改进建议（参考 FFplay `frame_drops_late` / MPV `--framedrop`）

```cpp
constexpr int kMaxConsecutiveDrops = 5;  // 连续丢帧上限
int consecutive_drops = 0;

if (diff < -kDropThreshold && consecutive_drops < kMaxConsecutiveDrops) {
    ++consecutive_drops;
    av_frame_unref(frame);
    continue;
} else {
    consecutive_drops = 0;
    // 强制显示，即使过期
}
```
确保用户至少每 N 帧能看到一次画面更新。

---

## 3. Seek/Replay from Finished 重建完整管线（重量级）

### 问题

当前从 `Finished` 状态 Seek 或 Play 时，需要 Stop 所有线程 → Reset 队列 → 重新 Start 全部线程。

### 影响场景

- **循环播放 (loop)**：每次循环都要销毁重建线程，带来不必要的开销和延迟
- **播放列表连续播放**：切换下一首时 latency 较高
- **短视频场景**：频繁到达 EOF 后立即重播

### 改进建议（参考 MPV 的常驻线程模型）

让 Demuxer/Decoder 线程在 EOF 时不退出，而是 wait 在条件变量上：
```cpp
// DemuxLoop 伪代码
while (running_) {
    int ret = av_read_frame(fmt_ctx_, pkt);
    if (ret == AVERROR_EOF) {
        push_null_packet();  // 通知 decoder drain
        wait_for_seek_or_close();  // 挂起，不退出
        continue;
    }
    ...
}
```
Seek 时只需唤醒线程 + flush queue，无需重建。VideoRenderLoop 类似处理。

---

## 4. PTS 缺失时无 duration-based 推算

### 问题

当 `frame->pts == AV_NOPTS_VALUE` 时直接赋值 `pts = 0.0`，后续同步逻辑全部失效。

### 影响场景

- **部分 AVI/MPEG-PS 容器**：某些旧格式 demuxer 不输出 pts
- **直播流中断恢复**：中间若干帧可能没有有效 pts

### 改进建议（参考 FFplay `frame->pts` fallback 链）

```cpp
double pts = 0.0;
if (frame->pts != AV_NOPTS_VALUE) {
    pts = frame->pts * av_q2d(stream->time_base);
} else if (frame->pkt_dts != AV_NOPTS_VALUE) {
    pts = frame->pkt_dts * av_q2d(stream->time_base);
} else {
    // 用上一帧 pts + duration 推算
    pts = last_pts + (video_fps_ > 0 ? 1.0 / video_fps_ : 0.04);
}
```

---

## 5. VideoRenderLoop 在视频 EOF 后直接退出

### 问题

视频流先到 EOF 时线程退出，后续如果想在视频结束后保持最后一帧显示（而音频继续），无法做到。

### 影响场景

- **音频比视频长的文件**：如 MV 结尾黑幕但有音乐余韵，视频线程已退出，无法响应任何 UI 操作（如截图最后一帧）
- **字幕渲染依赖视频线程**：如果未来加字幕，视频线程退出后字幕也会停止

### 改进建议（参考 MPV 的 `keep-open` / FFplay 保持最后帧）

视频 EOF 后不 break，而是进入"保持最后帧"等待：
```cpp
if (is_eof) {
    video_eof_.store(true);
    notify_eof();
    // 不 break，改为 wait 直到 Close/Seek 唤醒
    WaitIfPaused();  // 复用暂停等待逻辑
    if (state_.load() == PlayerState::Idle) break;
    continue;  // 被 Seek 唤醒后继续 pop
}
```
