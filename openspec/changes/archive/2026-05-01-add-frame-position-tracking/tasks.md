## 1. Core 层：添加 video_pts_ 和 video_fps_

- [x] 1.1 在 PlayerImpl 中添加 `std::atomic<double> video_pts_{0.0}` 成员
- [x] 1.2 在 PlayerImpl 中添加 `double video_fps_{0.0}` 成员，Open 时从 `avg_frame_rate` 初始化
- [x] 1.3 在 VideoRenderLoop 中，每次渲染帧后执行 `video_pts_.store(pts, std::memory_order_relaxed)`

## 2. Player 公共 API

- [x] 2.1 在 `player.h` 中声明 `double CurrentVideoPosition() const` 和 `double VideoFps() const`
- [x] 2.2 在 `player.cc` 中实现这两个方法（读取 atomic / 返回缓存值）

## 3. UI 层：帧位置显示

- [x] 3.1 在 MainWindow 中添加帧号显示 QLabel
- [x] 3.2 修改 OnTimerTick，读取 `CurrentVideoPosition()` 更新进度条
- [x] 3.3 在 OnTimerTick 中计算帧号 `frame = video_pos * fps`，更新帧号 Label

## 4. 验证

- [x] 4.1 编译通过，无 warning
- [x] 4.2 播放视频确认进度条和帧号正确更新
- [x] 4.3 Seek 后帧号正确跳转
