## ADDED Requirements

### Requirement: 硬件解码帧池由解码器按设备细化
DecoderNode 带硬件打开时 SHALL 设置 `extra_hw_frames`,为下游在途帧(链路深度 + sink 当前帧)预留解码 surface。`get_format` 选中设备硬件格式时 SHALL 自行创建帧池:`avcodec_get_hw_frames_parameters` 生成参数、补齐 FFmpeg 自动路径保证的工作 surface 数、调用 `GpuDevice::RefineDecoderFrames`、`av_hwframe_ctx_init`;任一步失败 SHALL 记录日志并退回 FFmpeg 默认帧池,不影响解码。解码器 SHALL 只依赖 `GpuDevice` 接口,不出现平台类型。

#### Scenario: 细化后的帧池可被 GPU pass 读取
- **WHEN** 硬件解码打开且设备支持对解码格式采样
- **THEN** 解码帧所在纹理带着色器资源绑定,GPU pass 直接读取其切片

#### Scenario: 细化失败回退默认帧池
- **WHEN** 自建帧池的 `av_hwframe_ctx_init` 失败
- **THEN** 记录告警,FFmpeg 以默认帧池继续硬件解码

### Requirement: 解码器不持有设备锁
DecoderNode SHALL NOT 在应用层为 `avcodec_send_packet` / `avcodec_receive_frame` / `avcodec_flush_buffers` 加设备锁:FFmpeg 通过交给它的递归锁在内部保护其命令上下文序列。DecoderNode SHALL 直接推送 FFmpeg 输出的原生硬件帧,不做呈现准备或下载。

#### Scenario: 解码不阻塞渲染
- **WHEN** 4K 关键帧在解码线程解析码流
- **THEN** 渲染线程只在 FFmpeg 真正提交 D3D11 命令的短区间内等待设备锁

#### Scenario: 推送原生硬件帧
- **WHEN** 解码出 D3D11 帧
- **THEN** 推送的 MediaFrame 即 FFmpeg 输出帧(数组纹理切片),呈现与下载由下游决定

## REMOVED Requirements

### Requirement: 硬件帧在解码线程完成呈现准备
**Reason**: 前提"设备命令上下文只能在解码线程使用"在开启多线程保护并统一递归锁后不再成立;呈现准备是渲染器关注点。
**Migration**: 渲染器持有 `FramePresenter` 在渲染线程完成数组纹理拷贝;不可绑定布局由渲染器在锁内下载。
