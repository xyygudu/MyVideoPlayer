## ADDED Requirements

### Requirement: Decoder supports skip_frame during seek

Decoder SHALL 在 seek 期间（`drop_until_pts_` 被设定后）将 `codec_ctx_->skip_frame` 设为 `AVDISCARD_NONREF`，令 FFmpeg 跳过非参考帧的解码。当解码出的帧 pts ≥ `drop_until_pts_` 时，Decoder SHALL 恢复 `codec_ctx_->skip_frame` 为 `AVDISCARD_DEFAULT`。

skip_frame 的设置和恢复 SHALL 仅在 DecodeLoop 线程中执行（单线程写），避免竞态。

#### Scenario: skip_frame is set after seek
- **WHEN** Decoder 检测到 serial change 且 `drop_until_pts_` > 0
- **THEN** `codec_ctx_->skip_frame` 被设为 `AVDISCARD_NONREF`

#### Scenario: skip_frame is restored when target reached
- **WHEN** Decoder 解码出一帧 pts ≥ `drop_until_pts_`
- **THEN** `codec_ctx_->skip_frame` 被恢复为 `AVDISCARD_DEFAULT`

#### Scenario: skip_frame has no effect for unsupported codecs
- **WHEN** codec 不支持 skip_frame hint
- **THEN** 设置被忽略，解码正常继续，无副作用

### Requirement: Decoder drops frames before target pts

Decoder SHALL 提供 `SetDropUntilPts(double pts)` 接口。当 `drop_until_pts_` > 0 时，Decoder DecodeLoop 中解码出的帧如果 `frame_pts < drop_until_pts_`，SHALL 直接 unref 不入 FrameQueue。

当解码出的帧 pts ≥ `drop_until_pts_` 时，Decoder SHALL 自动清除 `drop_until_pts_`（设为 0 或负值），后续帧正常入队。

`drop_until_pts_` SHALL 使用 atomic 存储，允许 Player 线程写入、Decoder 线程读取，无锁。

#### Scenario: Frames before target are dropped
- **WHEN** `drop_until_pts_` = 5.0 且 Decoder 解码出帧 pts = 3.2
- **THEN** 该帧被 unref，不入 FrameQueue

#### Scenario: Target frame is pushed to queue
- **WHEN** `drop_until_pts_` = 5.0 且 Decoder 解码出帧 pts = 5.1
- **THEN** 该帧正常 Push 到 FrameQueue，且 `drop_until_pts_` 被清除

#### Scenario: Subsequent frames after target are normal
- **WHEN** `drop_until_pts_` 已被清除
- **THEN** 后续所有帧正常入 FrameQueue，skip_frame 为 AVDISCARD_DEFAULT

#### Scenario: SetDropUntilPts is thread-safe
- **WHEN** Player 线程调用 `SetDropUntilPts(5.0)` 同时 Decoder 线程在读取 `drop_until_pts_`
- **THEN** 无数据竞争（atomic 保证）

## MODIFIED Requirements

### Requirement: Decoder flushes codec on serial change
Decoder SHALL 记录上次处理的 packet serial（`last_serial_`）。当 Pop 到一个 serial 不等于 `last_serial_` 的 packet 时，Decoder SHALL 先调用 `avcodec_flush_buffers` 清空 codec 内部缓存，然后更新 `last_serial_`。

**新增行为**: 在 flush 后，如果 `drop_until_pts_` > 0，Decoder SHALL 设置 `codec_ctx_->skip_frame = AVDISCARD_NONREF`。

Decoder Push frame 时 SHALL 传入 `last_serial_` 作为 frame 的 serial。

#### Scenario: Serial change triggers codec flush
- **WHEN** Decoder pop 到一个 packet 且其 serial != last_serial_
- **THEN** Decoder 执行 `avcodec_flush_buffers`，更新 last_serial_，然后解码该 packet

#### Scenario: Serial change with active drop target enables skip_frame
- **WHEN** Decoder pop 到一个 packet 且其 serial != last_serial_ 且 `drop_until_pts_` > 0
- **THEN** Decoder 执行 flush，设置 `skip_frame = AVDISCARD_NONREF`，然后解码

#### Scenario: Same serial does not flush
- **WHEN** Decoder pop 到一个 packet 且其 serial == last_serial_
- **THEN** Decoder 直接解码，不执行 flush
