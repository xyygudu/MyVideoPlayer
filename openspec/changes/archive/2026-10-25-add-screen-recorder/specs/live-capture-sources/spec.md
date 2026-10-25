## ADDED Requirements

### Requirement: LiveSourceNode 在设备线程上管理设备
系统 SHALL 提供 `LiveSourceNode` 基类（Source 类型、Active）。`Open()` SHALL 启动设备线程并等待其在该线程调用 `OpenDevice()` 的结果：成功时以设备报告的格式作为输出端口格式（媒体类型写入输出 caps），失败时节点进入 `kError`、线程结束、`Open()` 返回 false。设备线程 SHALL 等待 `Start()` 后反复调用 `CaptureStep()`，结束时在同一线程调用 `CloseDevice()`。派生类 SHALL 在析构函数中调用 `Stop()`。

#### Scenario: 设备打不开
- **WHEN** 音频源的设备 id 不存在
- **THEN** `Open()` 返回 false，节点 `State()` 为 `kError`，没有遗留线程

#### Scenario: 打开但未启动就停止
- **WHEN** 图 Open 成功、Negotiate 失败后调用 `MediaGraph::Stop()`
- **THEN** 设备线程不进入采集，关闭设备后退出

### Requirement: 实时源共享主时钟并冻结到启动
每个 `LiveSourceNode` SHALL 通过 `ProvideClock()` 提供一个优先级为 50 的 `Clock`；`Prepare()` SHALL 将其复位到 0 并暂停，`Start()` SHALL 解除暂停。节点 SHALL 用 `MediaGraph::MasterClock()`（无主时钟时用自己的时钟）的当前值为数据打时间戳，使同一张图内所有实时源处于同一时间轴。

#### Scenario: 多个源同轴
- **WHEN** 屏幕源与系统声音源同时录制 2 秒
- **THEN** 两路时间戳都从 0 附近开始，结束时刻相差不超过一个视频帧间隔与一个音频块

### Requirement: 实时源在暂停时丢弃数据
`SetPaused(true)` 后 `Emit()` SHALL 丢弃数据（返回 true）；图暂停同时冻结所有时钟，因此恢复后时间戳从暂停时刻继续。`Emit()` SHALL 在下游链路已中止时返回 false，并使 `Capturing()` 变为 false。

#### Scenario: 暂停时长不进入文件
- **WHEN** 录制 1 秒、暂停 1 秒、再录制 1 秒后停止
- **THEN** 输出视频时长约 2 秒，约 60 帧（30fps）

### Requirement: kEndOfStream 结束实时流
收到 `OnCommand({kEndOfStream})` 时，实时源 SHALL 停止采集、在设备线程推送一个带当前 seek 世代的 EOS 缓冲，然后关闭设备；`InterruptDevice()` SHALL 让阻塞在设备读取中的 `CaptureStep()` 尽快返回。`Stop()` SHALL 中止采集而不推送 EOS。

#### Scenario: 优雅结束
- **WHEN** Recorder 广播 kEndOfStream
- **THEN** 屏幕、音频、摄像头节点各推送一次 EOS，日志记录 "end of stream"

### Requirement: ScreenCaptureNode 以恒定时隙采集
`ScreenCaptureNode(VideoCaptureOptions)` SHALL 输出 `kRGB32`（内存 BGRA）帧，格式帧率为 `frame_rate`（限制在 1..120）。第 n 帧 SHALL 在运行时间到达 n/fps 时采集、pts 为 n/fps；采集落后时 SHALL 跳到当前时隙，把跳过的时隙计入 `FramesDropped()`，不补发。`FramesCaptured()` SHALL 统计成功送出的帧数。屏幕无法再采集时 SHALL 经总线上报 kError。

#### Scenario: 帧数与时长一致
- **WHEN** 以 30fps 录制 2 秒 640x360 区域
- **THEN** 输出约 60 帧（允许少量丢帧），时间戳都在 1/30 秒网格上

### Requirement: 屏幕采集后端
`capture::CreateScreenSource()` SHALL 按 `CaptureMode` 解析目标：`kMonitor`（显示器 id，空为主显示器）、`kRegion`（物理像素矩形）、`kWindow`（窗口可见边框，大小以开始时为准、位置每帧跟随，最小化/关闭时保持最后一帧）。宽高 SHALL 向下取偶，小于 16 时打开失败。Windows 实现 SHALL 在 `kAuto`/`kDesktopDuplication` 且区域完全位于一块显示器内时使用 DXGI 桌面复制，否则或其不可用（旋转屏、非 8 位 BGRA 桌面图像、复制失败）时使用 GDI `BitBlt(CAPTUREBLT)`；DXGI 运行中失效 SHALL 自动切换到 GDI。采集线程 SHALL 使用每显示器 DPI 感知，并在采集期间将系统计时器精度设为 1ms。

#### Scenario: 指定桌面复制
- **WHEN** 以 `kDesktopDuplication` 录制主显示器内的区域
- **THEN** 日志显示 "DXGI desktop duplication"，录像内容与屏幕一致

#### Scenario: 空区域
- **WHEN** 区域宽或高为 0
- **THEN** 屏幕源打开失败，`Recorder::Start()` 返回 false

### Requirement: 光标与点击高亮绘制在画面中
`capture_cursor` 为真时，屏幕源 SHALL 在每帧画布上按当前光标形状与热点绘制鼠标指针；`highlight_clicks` 为真且左/右键按下时，SHALL 先在光标位置绘制半透明光圈（左键黄色、右键蓝色，半径随系统 DPI 缩放）。

#### Scenario: 录到指针
- **WHEN** 鼠标停在录制区域内
- **THEN** 录像中可见与屏幕相同的指针

### Requirement: AudioCaptureNode 采集环回或麦克风
`AudioCaptureNode(kind, device_id)` SHALL 以设备混音格式输出音频帧（float/s16/s32 交错），每 10ms 读取一次设备缓冲。`kOutput` SHALL 以 WASAPI 环回采集该输出设备正在播放的声音，`kInput` 采集麦克风；`device_id` 为空时使用系统默认设备。每个包的 pts SHALL 为 `Now() − age`，`age` 由设备给出的首样本 QPC 时间戳计算（限制在 0..1 秒）。设备失效（拔出、禁用）时 SHALL 关闭并每秒重试打开，期间不产生数据、不上报错误。

#### Scenario: 拔出耳机
- **WHEN** 录制中默认输出设备被拔出
- **THEN** 录制继续，该段音频为静音；设备恢复（或默认设备切换后重开）后继续采集

### Requirement: CameraCaptureNode 采集摄像头
`CameraCaptureNode(device_id)` SHALL 输出 BGRA 帧，pts 为帧到达时的运行时间。Windows 实现 SHALL 用 Media Foundation Source Reader（开启视频处理，以便解码 MJPEG 并转换为 RGB32），选择帧率不低于 24 中面积最接近 1280x720 的原生模式。读取失败或格式变化时 SHALL 关闭并每秒重开；`Interrupt()` 之后的所有读取 SHALL 立即返回。

#### Scenario: 摄像头被占用
- **WHEN** 录制中摄像头被其他程序独占
- **THEN** 节点每秒重试，录制不中断

### Requirement: CaptureDevices 枚举可录制的对象
`mvp::CaptureDevices` SHALL 提供：`Monitors()`（物理像素边界、恰有一个主显示器且原点为 (0,0)）、`Windows()`（可见、有标题、非工具窗口、未被 DWM 隐藏的顶层窗口，按 Z 序，边界为不含阴影的可见边框）、`WindowBounds(id)`（窗口关闭或最小化时为空）、`Cameras()`、`AudioDevices(kind)`（活动端点，标记默认设备）、`VideoEncoders()`（逐个打开 h264_nvenc、h264_qsv、h264_amf、libx264 会话，返回能打开的，硬件在前）。

#### Scenario: 编码器探测
- **WHEN** 机器有 NVIDIA 显卡但没有 Intel/AMD 编码器
- **THEN** `VideoEncoders()` 返回 {"h264_nvenc", "libx264"}
