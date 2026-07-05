## ADDED Requirements

### Requirement: demux seek 请求不可丢失

DemuxNode 的 seek 请求机制 SHALL 保证：在任意快速连续 seek 序列下，demux 线程最终 seek 到的位置 SHALL 与最后一次 seek 命令的目标一致，不得因并发丢失最新请求。

#### Scenario: 处理旧请求时到来新请求不被吞掉

- **WHEN** demux 线程正在处理 seek 到 A，期间主线程请求 seek 到 B
- **THEN** demux 完成 A 后 SHALL 继续 seek 到 B（不会停在 A）

#### Scenario: 多次快速 seek 只生效最新目标

- **WHEN** 主线程连续请求 seek 到 X1, X2, X3
- **THEN** demux 最终 seek 到的位置 SHALL 是 X3

### Requirement: demux 与 decoder 的 seek 目标一致

在任意 seek 序列结束后，DemuxNode 实际 seek 到的位置与各 DecoderNode 的 drop 目标 SHALL 对应同一次 seek 命令，二者不得指向不同的 seek。

#### Scenario: 快速 seek 后不出现目标错配

- **WHEN** 用户连续快速 seek 多次后停止
- **THEN** demux 喂出的包区间与 decoder 的 drop_until_pts SHALL 匹配，解码器不会长时间丢弃所有帧

### Requirement: seek flush 隔离陈旧在途包

seek flush 之后，seek 前读出但尚未入队（阻塞在 Link Push）的 pre-seek 包 SHALL NOT 被当作 post-seek 有效数据交给解码器。

#### Scenario: 被唤醒的陈旧包被丢弃

- **WHEN** 一个 pre-seek 包阻塞在 Link::Push，此时发生 seek flush
- **THEN** 该包 SHALL 因 serial 落后于 Link 当前 serial 而被丢弃，不进入解码器

#### Scenario: flush 后首个解码包为 post-seek 数据

- **WHEN** seek 完成后解码器从 Link 取包
- **THEN** 解码器 SHALL NOT 在 avcodec_flush_buffers 之后立即解码到 pre-seek 的 GOP 中间帧

### Requirement: seek 控制路径线程安全

seek 命令的响应 SHALL NOT 从非解码线程写 `AVCodecContext`。`SetDropUntilPts` SHALL 只更新原子 drop 目标，`skip_frame` 等 codec 字段 SHALL 仅由解码线程设置。

#### Scenario: 主线程不触碰 codec_ctx

- **WHEN** 主线程通过 OnCommand 调用 SetDropUntilPts
- **THEN** 该调用 SHALL NOT 写 codec_ctx_->skip_frame，仅原子写 drop_until_pts_
