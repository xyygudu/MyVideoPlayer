## Why

4K 视频播放中连续快速 seek 会导致画面和音频冻结数十秒（需 re-seek 才能恢复）。日志实证：最后一次 seek 让解码器 `drop_until_pts=472.752`，但 demux 实际只 seek 到 `363.607`——两者相差 109 秒。demux 从 363.607 往前喂包，解码器却把所有帧丢弃苦等 472.752，形成长时间"假死"。

根因是 **demux 与 decoder 的 seek 目标在快速 seek 下会 desync**：demux 用两个独立原子（`seek_position_` + `seek_requested_`）传递 seek，处理完后 `seek_requested_.store(false)` 会**吞掉**并发到来的新 seek 请求（lost-update 竞态），而 decoder 每次 seek 都同步更新 drop target，于是二者目标不一致。

## What Changes

- **修复 seek 请求 lost-update**：demux 的 seek 请求改为不可丢失的语义（单调 epoch 或等价机制），确保 demux 永远 seek 到最新目标，与 decoder 的 drop target 一致
- **消除陈旧在途包**：seek flush 时，被阻塞在 `Link::Push` 中的 pre-seek 包不得被盖上新 serial 混入 post-seek 数据流（当前会导致 flush 后立即解码 GOP 中间帧，产生 `Missing reference picture` 等 h264 报错）
- **修复跨线程 codec 访问**：`DecoderNode::SetDropUntilPts` 不得在主线程写 `codec_ctx_->skip_frame`（该字段只能由解码线程访问）；skip_frame 已由解码线程的 `MaybeFlushOnSerialChange` 正确设置，主线程的写是冗余且不安全的
- 移除本次调查期间添加的 `[SEEK-DIAG]` 临时日志

## Capabilities

### New Capabilities

- `seek-consistency`: seek 目标在 demux/decoder 间的一致性保证，含不可丢失的 seek 请求、陈旧包隔离、seek 控制路径的线程安全

### Modified Capabilities

<!-- 无既有 spec 级行为被改写；本次为新增一致性保证 -->

## Impact

- **代码**：`demux_node.{h,cc}`（seek 请求机制）、`link.h`（陈旧包/serial 语义）、`decoder_node.cc`（去除跨线程写）、`main.cc`（移除临时文件日志）
- **线程安全**：修正一处主线程↔解码线程对 `codec_ctx_` 的数据竞争
- **行为**：连续快速 seek 不再冻结；seek 时 h264 报错消除
