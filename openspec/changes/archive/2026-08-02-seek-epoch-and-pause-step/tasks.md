## 1. 世代提升到图级

- [x] 1.1 `MediaGraph` 新增 `std::atomic<int> seek_epoch_` 与 `int SeekEpoch() const`；`Seek()` 中先 `fetch_add(1)` 再 `Flush()`
- [x] 1.2 `InputPort` 新增 `const std::atomic<int>* seek_epoch_` 与 `BindSeekEpoch()`；`MediaGraph::Connect()` 连接成功后绑定
- [x] 1.3 `Link` 移除 `serial_` / `serial()`，`Flush()` 与 `Reset()` 不再操作世代
- [x] 1.4 `DemuxNode::RefreshLocalSerial()` 改为从 `graph_->SeekEpoch()` 锁存，去掉端口参数

## 2. 校验下沉到端口边界

- [x] 2.1 `InputPort::Pull()` 丢弃世代过期的 buffer 并 `SPDLOG_DEBUG`
- [x] 2.2 `InputPort::Pull()` 丢弃 `!IsValid()` 的 buffer 并 `SPDLOG_WARN`
- [x] 2.3 删除 `DecoderNode::DecodeLoop` 中的手工世代检查
- [x] 2.4 确认 `MediaBuffer::IsValid()` 对 EOS-only buffer 返回 true，EOS 可正常通过

## 3. 世代透传补齐

- [x] 3.1 `OutputPort::Push` 对 Passive 下游，由 emit 回调自动继承输入 buffer 的世代
- [x] 3.2 删除 `TransformEffectNode`（2 处）与 `ColorEffectNode`（1 处）的手工 `set_serial`
- [x] 3.3 `MediaBuffer::MakeEos(MediaType, int serial)` 改为必传世代
- [x] 3.4 `DecoderNode` 新增 `current_serial_`，在 `DecodeLoop` 中从输入 buffer 锁存
- [x] 3.5 `DecoderNode::DrainFrames()` 与 `HandleEos()` 给输出 buffer 打标
- [x] 3.6 `DemuxNode::EmitEos()` 改用新的 `MakeEos` 签名
- [x] 3.7 `EncoderNode` 同样锁存并透传世代（转码图当前不 seek，但保持与 DecoderNode 对称）

## 4. 暂停态 step 与重绘

- [x] 4.1 `CommandType` 新增 `kRedraw`
- [x] 4.2 `VideoSinkNode` 新增 `MediaFrame current_frame_`；`RenderFrame` 改为接收右值并移动存入
- [x] 4.3 `awating_preview_frame_` 改名为 `step_`，语义对齐 ffplay `step_to_next_frame`；`SetPaused(false)` 时清除
- [x] 4.4 `VideoSinkNode::OnCommand` 处理 `kRedraw`，暂停循环中重绘 `current_frame_`
- [x] 4.5 `MediaPlayer::Impl::NotifyWindowResized` 广播 `kRedraw`
- [x] 4.6 CMakeLists 定义 `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG`（Debug/RelWithDebInfo）—— 此前编译期默认 INFO，所有 `SPDLOG_DEBUG` 被预处理器删除，与 `logging.cc` 运行期设的 debug 级别矛盾

## 5. 验证

- [x] 5.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 5.2 `mvp_transcode_cli` 回归：mkv/mpegts 均完整转码，输出字节数与改动前完全一致，无 warning
- [x] 5.3 人工验收 —— **暂停后 seek**：显示的是目标位置的帧，而非 seek 前的帧
- [x] 5.4 人工验收 —— **播放中 seek**：画面与声音跳到目标位置且同步
- [x] 5.5 人工验收 —— **播放到结尾**：正常报结束（日志证据：结尾后的 play 触发 `seek to 0.00s` 重播分支，且该次 seek 无任何丢弃——队列已排空）
- [x] 5.6 人工验收 —— **连续快速 seek**：不卡死、不黑屏，世代严格递增
- [ ] 5.7 人工验收 —— **暂停态拖拽窗口**：画面立即重新适配窗口尺寸
- [x] 5.8 开启 DEBUG 日志确认 seek 时确有 "drop stale buffer" 记录，且 `serial == epoch - 1`；无 "drop malformed buffer" WARN
