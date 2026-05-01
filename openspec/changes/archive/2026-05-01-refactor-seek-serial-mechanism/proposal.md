## Why

当前 Seek 的 flush 机制依赖并发 flag（`flush_requested_`/`flush_completed_`/`step_one_frame_`）和多次 Flush 调用来避免竞态，逻辑分散在 Player、Decoder、Demuxer 三处，维护困难。业界标准做法（FFplay/mpv）使用 serial（纪元计数器）让旧数据自然失效，无需显式 flush frame_queue，也无需 `flush_completed_` 等待。

## What Changes

- PacketQueue / FrameQueue 新增 `serial_` 计数器，每次 seek 递增
- Packet 和 Frame 入队时携带当前 serial
- Decoder pop packet 时比对 serial，不匹配则丢弃
- Render loop pop frame 时比对 serial，不匹配则丢弃
- **移除** `flush_requested_`、`flush_completed_`、Demuxer 内部的二次 Flush
- **保留** Player 侧的 queue Flush，但与 serial++ 合并为原子操作 `FlushAndIncrementSerial()`（对齐 FFplay `packet_queue_flush`）
- 保留 `avcodec_flush_buffers` 但改为通过 serial 变化触发（decoder 检测到 serial 跳变时执行）
- 保留 `step_one_frame_` 用于暂停状态 seek 显示帧

## Capabilities

### New Capabilities

（无新增用户可见能力）

### Modified Capabilities

- `demux-decode`: PacketQueue/FrameQueue 新增 serial 机制，Decoder 按 serial 丢弃过期数据并 flush codec

## Impact

- `src/core/src/packet_queue.h/cc` — 添加 serial 字段和 IncrementSerial 接口
- `src/core/src/frame_queue.h/cc` — 添加 serial 字段和 IncrementSerial 接口
- `src/core/src/decoder.h/cc` — 移除 flush_requested_/flush_completed_，改为 serial 比对触发 flush
- `src/core/src/player.cc` — Seek() 简化为递增 serial + 设 step flag
- `src/core/src/demuxer.cc` — DemuxLoop 中移除 seek 后的 Flush 调用
- 公共 API 无变化，行为完全兼容
