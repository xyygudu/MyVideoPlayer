## 1. 音频时钟报告已呈现位置

- [x] 1.1 `AudioSinkNode` 新增 `double QueuedSeconds() const`，由 SDL 队列字节数换算
- [x] 1.2 `ShouldThrottle()` 改用 `QueuedSeconds() > kQueueTargetSeconds`，`/10` 提为具名常量
- [x] 1.3 `AudioLoop` 在 `ConvertAndFeed` **之前**取队列深度，`clock_->Set(mf.pts() - queued)`

## 2. 呈现与时基推进分离

- [x] 2.1 `RenderFrame` 更名 `PresentFrame`，移除 `clock_->Set`，只保存当前帧并渲染
- [x] 2.2 `SyncAndRender` 在 sleep 之后、`PresentFrame` 之前显式 `clock_->Set(pts)`
- [x] 2.3 暂停分支同样显式推进时基后再 `PresentFrame`

## 3. 尺寸变化走命令并在渲染线程应用

- [x] 3.1 `CommandType::kRedraw` 改为 `kResize`；`Command` 新增 `width` / `height`
- [x] 3.2 `VideoSinkNode` 以单个 `std::atomic<uint64_t> pending_size_` 暂存待应用尺寸
- [x] 3.3 `OnCommand` 处理 `kResize` 仅暂存；渲染循环应用 `renderer_->Resize` 并 `RedrawCurrent()`
- [x] 3.4 `MediaPlayer::Impl::NotifyWindowResized` 删除直接 `video_renderer_.Resize()`，改为广播 `kResize`
- [x] 3.5 确认 `VideoRenderer::window_width_/height_` 此后仅被渲染线程写（竞态消除，无需原子化）

## 4. Flush 自持世代不变量

- [x] 4.1 `MediaGraph::Flush()` 内先递增 `seek_epoch_` 再清空 Link
- [x] 4.2 `MediaGraph::Seek()` 移除自身的世代递增，简化为 `Flush + SendCommand + 重置时钟`

## 5. 验证

- [x] 5.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 5.2 `mvp_transcode_cli` 回归：mkv 70,534,409B / mpegts 78,101,028B，与改动前逐字节一致，无 warning
- [x] 5.3 人工验收 —— **音画同步**：时钟后移 106ms 后无感知到的回归，未出现视频滞后（测试片源为动画，唇音参考较弱；结合改动前同样无感知，说明 106ms 在该内容上低于感知阈值）
- [x] 5.4 人工验收 —— **窗口缩放**：播放中与暂停态均立即重新适配
- [x] 5.5 人工验收 —— **seek 后时钟无跳变**：进度条与画面一致，无回跳
- [x] 5.6 人工验收 —— **播放到结尾**报结束并可重播（确认 Flush 语义变更未破坏 EOS 路径）
- [x] 5.7 DEBUG 日志确认 seek 时仍有 `drop stale buffer` 且 `serial == epoch - 1`
