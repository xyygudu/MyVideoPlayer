# screen-recorder Specification

## Purpose
Defines the `mvp::Recorder` and `mvp::CameraPreview` facades: how a
recording graph is built, degraded when optional devices fail, paused,
finished and reported.
## Requirements
### Requirement: Recorder 构建录屏图
`mvp::Recorder::Start(RecordOptions)` SHALL 建立图：`ScreenCaptureNode`（启用摄像头时经 `VideoCompositorNode` 与 `CameraCaptureNode` 合成）→ 视频 `EncoderNode` → `MuxNode` 视频输入；启用系统声音或麦克风时，各自的 `AudioCaptureNode` → `AudioMixerNode` → 音频 `EncoderNode` → `MuxNode` 音频输入。容器由输出文件扩展名决定。图 Open/Negotiate/Prepare/Start 全部成功后 SHALL 返回 true 并进入 `kRecording`；任一步失败 SHALL 返回 false、状态保持 `kIdle`、不调用完成回调。录制中再次 `Start()` SHALL 被拒绝。

#### Scenario: 屏幕加系统声音
- **WHEN** 录制主显示器并启用系统声音 5 秒后停止
- **THEN** 文件含 H.264 视频与 48kHz 立体声 AAC 音频，两者时长相差小于 0.1 秒

### Requirement: Recorder 选择实时编码参数
Recorder SHALL 只采用 `video_encoder` 的编码器名与 `crf`：码率控制固定为恒定质量；预设由编码器决定（libx264 `veryfast`、*_nvenc `p4`、*_qsv `veryfast`、其余不设）；GOP 为 2 秒的帧数；libx264 最多 2 个 B 帧，硬件编码器不使用 B 帧。音频使用 `audio_encoder` 原样参数。

#### Scenario: NVENC
- **WHEN** 选择 h264_nvenc、画质 23
- **THEN** 编码器以 CQ 23、预设 p4 打开，GOP 为帧率 × 2

### Requirement: 打不开的可选源被剔除
图 Open 失败且失败节点（`State()==kError`）是摄像头、麦克风或系统声音时，Recorder SHALL 记录告警、在选项中去掉该源并重建图重试，直到成功或失败的是屏幕/输出。`Sources()` SHALL 报告本次录制实际包含的系统声音、麦克风、摄像头。

#### Scenario: 不存在的麦克风与摄像头
- **WHEN** 麦克风与摄像头都给了不存在的设备 id
- **THEN** 录制照常进行，`Sources()` 中二者为 false，文件没有音频流

### Requirement: Recorder 暂停与音量
`Pause()`/`Resume()` SHALL 在 `kRecording`/`kPaused` 间切换，通过 `MediaGraph::SetPaused` 冻结全部时钟，暂停的时长不进入文件。`SetVolume(source, volume)` SHALL 在录制中即时改变对应混音输入的增益。

#### Scenario: 暂停一秒
- **WHEN** 录 1 秒、暂停 1 秒、再录 1 秒
- **THEN** 文件约 2 秒

### Requirement: Recorder 停止与完成回调
`Stop()` SHALL 立即返回并进入 `kStopping`：广播 `kEndOfStream`，各源推 EOS，编码器冲刷，MuxNode 写尾后经总线上报 kEos，此时完成回调以 `kSucceeded` 调用一次并回到 `kIdle`。任何节点上报 kError，或停止后 15 秒仍未完成，SHALL 以 `kFailed` 完成（MuxNode 中止时仍写尾，已编码的内容可播放）。完成回调 SHALL 在内部线程调用，可在回调中销毁 Recorder 或开始新录制。析构 SHALL 中止录制且不调用回调。

#### Scenario: 正常停止
- **WHEN** 录制中调用 `Stop()`
- **THEN** `State()` 立即为 `kStopping` 或 `kIdle`，随后回调收到 `kSucceeded`，`State()` 为 `kIdle`

### Requirement: Recorder 统计
`Stats()` SHALL 返回：已录制时长（主时钟，暂停不计；`Stop()` 时冻结）、输出文件当前大小、屏幕已采集帧数与丢帧数、系统声音与麦克风自上次调用以来的峰值电平（已乘音量）。

#### Scenario: 录制中查询
- **WHEN** 录制 1.5 秒时调用 `Stats()`
- **THEN** 时长约 1.5 秒，采集帧数大于 25，文件大小大于 0

### Requirement: CameraPreview 预览摄像头
`mvp::CameraPreview::Start(device_id, max_width, on_frame)` SHALL 以 `CameraCaptureNode` → `FrameCallbackSinkNode` 两节点图打开摄像头，在内部线程把每帧转换为宽度不超过 `max_width` 的 RGBA `CapturedFrame` 交给回调；摄像头打不开时返回 false。`Stop()` SHALL 关闭摄像头并等待线程退出。

#### Scenario: 录制前预览
- **WHEN** 页面显示且启用了摄像头叠加
- **THEN** 预览区显示摄像头实时画面；开始录制前预览停止以释放摄像头

