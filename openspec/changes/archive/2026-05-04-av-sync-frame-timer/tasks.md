## 1. 新增 frame_timer_ 成员与初始化

- [x] 1.1 在 PlayerImpl 类中添加 `double frame_timer_{0.0}` 成员变量
- [x] 1.2 在 VideoRenderLoop 入口处初始化 `frame_timer_ = Clock::Now()`

## 2. 重写 ComputeDisplayDelay AudioMaster 分支

- [x] 2.1 移除 ComputeDisplayDelay 的 `const` 限定符
- [x] 2.2 实现新算法：计算 delay、diff、sync_threshold、修正 delay、累积 frame_timer_、计算 actual_wait
- [x] 2.3 实现 frame_timer 重置逻辑：actual_wait < -kMaxSleepSeconds 时重置为 Clock::Now()
- [x] 2.4 返回值语义：actual_wait > 0 时返回 min(actual_wait, kMaxSleepSeconds)；otherwise 返回 0.0（不再返回 -1.0 丢帧）

## 3. 调整 VideoRenderLoop 调用逻辑

- [x] 3.1 移除 AudioMaster 分支中对 `delay < 0` 的丢帧 continue 逻辑（frame_timer 重置机制替代）
- [x] 3.2 确认 last_pts 在 AudioMaster 模式下正确传递给 ComputeDisplayDelay

## 4. 构建验证

- [x] 4.1 编译通过，无 warning
- [x] 4.2 播放 25fps/60fps 视频验证画面流畅无 judder
- [x] 4.3 Seek 后验证第一帧立即显示，不出现连续丢帧
