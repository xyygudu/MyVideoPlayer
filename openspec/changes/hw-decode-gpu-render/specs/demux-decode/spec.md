## MODIFIED Requirements

### Requirement: Decoder flushes codec on serial change
Decoder SHALL 记录上次处理的 packet serial（`last_serial_`）。当 Pop 到一个 serial 不等于 `last_serial_` 的 packet 时，Decoder SHALL 先调用 `avcodec_flush_buffers` 清空 codec 内部缓存，然后更新 `last_serial_`。

在 flush 后，如果 `drop_until_pts_` > 0，Decoder SHALL 设置 `codec_ctx_->skip_frame = AVDISCARD_NONREF`。

**新增行为**: `Decoder::Open` SHALL 接受可选的 `HWAccelContext*` 参数。当 hw_ctx 非空时，Decoder SHALL：
1. 将 `av_buffer_ref(hw_ctx->DeviceRef())` 赋值给 `codec_ctx_->hw_device_ctx`
2. 将 hw_ctx 指针存入 `codec_ctx_->opaque`
3. 将 `codec_ctx_->get_format` 设为 `HWAccelContext::GetFormat`

解码流程（send_packet / receive_frame）不变。硬解与软解对 Decoder 而言透明——输出帧格式由 FFmpeg 内部决定。

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

#### Scenario: Open with HWAccelContext enables hardware decode
- **WHEN** 调用 `Decoder::Open(stream, hw_ctx)` 且 hw_ctx 非空
- **THEN** codec_ctx 配置了 hw_device_ctx 和 get_format 回调，解码输出可能为 D3D11 格式帧

#### Scenario: Open without HWAccelContext uses software decode
- **WHEN** 调用 `Decoder::Open(stream, nullptr)` 或 `Decoder::Open(stream)`
- **THEN** 行为与当前完全一致，纯软件解码
