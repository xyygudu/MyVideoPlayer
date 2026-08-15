## MODIFIED Requirements

### Requirement: DecoderNode queries HW device from graph
DecoderNode SHALL 移除 SetHWAccel 方法。帧域决策 SHALL 在 `Negotiate()` 完成:经 `graph_->GpuDevice()` 取设备,校验 `SupportsDecoder(codec)` 与下游端口 caps 是否接受设备域,满足才协商硬件域,否则软格式。

`Prepare()` SHALL 缓存图级设备引用,在打开解码器时挂载 `hw_device_ctx`(经 `av_buffer_ref`)、`get_format` 回调与 `opaque`。带硬件打开失败 SHALL 自动重试软件解码并记录日志。

#### Scenario: Hardware decode negotiated
- **WHEN** 图有 GPU 设备、编码器支持硬解、下游 caps 接受设备域
- **THEN** 输出端口协商为设备域,Prepare 打开带 hw_device_ctx 的解码器

#### Scenario: Downstream rejects hardware domain
- **WHEN** 下游 caps 不包含设备域(如渲染器后端不支持绑定)
- **THEN** 输出端口协商软格式,解码器不带硬件配置打开

#### Scenario: Hardware open fails, software retry
- **WHEN** 带硬件打开 `avcodec_open2` 失败
- **THEN** 释放该上下文,重试软件打开;再次失败才置 NodeState::kError

### Requirement: 硬件帧在解码线程完成呈现准备
DecoderNode SHALL 在解码线程(设备命令上下文的唯一使用者)完成硬件帧的呈现准备:调用 `GpuDevice::CopyForPresentation` 生成呈现纹理并挂到 MediaFrame;生成失败时 SHALL 在同一线程 `av_hwframe_transfer_data` 下载为软件帧后推送。解码线程之外的任何节点 SHALL NOT 对硬件帧做设备操作或下载转换。

#### Scenario: 硬件帧携带呈现纹理
- **WHEN** CopyForPresentation 成功
- **THEN** 推送的 MediaFrame 携带非空呈现纹理,渲染线程只做绑定与呈现

#### Scenario: 下载回退保持单线程契约
- **WHEN** CopyForPresentation 返回 nullptr 且下载成功
- **THEN** 推送软件帧(实际格式经 MaybeAnnounceFormat 校正),渲染走软件上传路径;下载失败则丢帧告警

### Requirement: Decoder supports skip_frame during seek
DecoderNode SHALL 在 seek 追赶期设置 `codec_ctx_->skip_frame = AVDISCARD_NONREF` 加速软件解码,到达目标 PTS 后恢复 AVDISCARD_DEFAULT。硬件解码(`codec_ctx_->hw_device_ctx` 非空)SHALL NOT 设置 skip_frame:硬解 surface 池在输出被丢弃时会泄漏,池耗尽后 `avcodec_send_packet` 永久阻塞等待空闲 surface;硬解追赶仅靠 PTS 阈值丢帧(帧完整解码后丢弃,surface 正常归还)。

#### Scenario: 软件解码 seek 追赶跳帧
- **WHEN** 软件解码且 seek 后尚未到达目标 PTS
- **THEN** skip_frame 置 AVDISCARD_NONREF,到达目标后恢复 AVDISCARD_DEFAULT

#### Scenario: 硬件解码 seek 不跳帧
- **WHEN** 硬件解码(带 hw_device_ctx)时 seek
- **THEN** skip_frame 保持默认值,追赶期只按 PTS 阈值丢弃已解码帧

### Requirement: DecoderNode Negotiate 做格式推理
DecoderNode::Negotiate SHALL 从 EncodedFormat::codec_params 推理输出格式,不开 codec。Prepare SHALL 只剩资源分配。像素格式为占位,首帧后 SHALL 按实际 `AVFrame::format` 校正输出端口格式(含硬件帧的 hw_sw_format),格式变化时经 `OutputPort::SetFormat` 传播。

#### Scenario: Negotiate 算出输出格式不开 codec
- **WHEN** DecoderNode::Negotiate 执行
- **THEN** 从输入端口的 codec_params 构造输出 VideoFormat,未调用 avcodec_open2

#### Scenario: 运行时校正实际格式
- **WHEN** 解码出首个与实际格式不同于协商占位值的帧
- **THEN** 输出端口格式被更新为实际像素格式(硬件帧附带 hw_sw_format),后续同格式帧不再更新
