## ADDED Requirements

### Requirement: EncoderNode 按 BT.709 编码 RGB 源
输入为 `kRGB32` 而编码器像素格式为 YUV 时，EncoderNode SHALL 在编码器上下文写入 BT.709 色彩标记（色彩空间、原色、传输特性）与有限范围，并以 BT.709 系数、有限范围配置 swscale 的 RGB→YUV 转换；源尺寸或格式变化导致转换上下文重建后 SHALL 重新应用该配置。YUV 输入的行为不变。

#### Scenario: 屏幕录像的色彩标记
- **WHEN** 录制屏幕（BGRA）编码为 H.264
- **THEN** ffprobe 显示 color_space=bt709、color_primaries=bt709、color_transfer=bt709、color_range=tv

### Requirement: EncoderNode 映射硬件编码器的恒定质量
`RateControlMode::kCrf` 时 EncoderNode SHALL 按编码器名映射恒定质量参数：`*_nvenc` 设置 `rc=vbr`、`cq=crf` 并把码率置 0（否则默认 200kb/s 会限制画质）；`*_qsv` 设置 `global_quality=crf`；`*_amf` 设置 `rc=cqp` 与 `qp_i/qp_p/qp_b=crf`；其余编码器设置 `crf`。

#### Scenario: NVENC 恒定质量
- **WHEN** 以 h264_nvenc、crf 23 打开编码器
- **THEN** FFmpeg 日志显示 CQ 模式启用，编码器打开成功
