## 1. 图引擎扩展

- [x] 1.1 `Link::TryPop`（与 `Pop` 共用 `TakeFront`）、`InputPort::TryPull`（与 `Pull` 共用 `Usable` 过滤）
- [x] 1.2 `CommandType::kEndOfStream`
- [x] 1.3 `MessageBus::WaitFor/IsShutDown`

## 2. 公共接口

- [x] 2.1 `captured_frame.h`（`CapturedFrame` 从 `media_player.h` 移出）
- [x] 2.2 `capture_devices.h`：`DesktopRect`、`MonitorInfo`、`WindowInfo`、`CameraInfo`、`AudioDeviceInfo`、`CaptureDevices`
- [x] 2.3 `recorder.h`：`VideoCaptureOptions`、`AudioCaptureOptions`、`CameraOverlayOptions`、`RecordOptions`、`RecorderState`、`RecordResult`、`RecordingStats`、`RecordingSources`、`Recorder`
- [x] 2.4 `camera_preview.h`：`CameraPreview`

## 3. Windows 采集后端

- [x] 3.1 `capture/screen_source.h`、`audio_source.h`、`camera_source.h` 接口与工厂
- [x] 3.2 `win_util`：UTF-8/宽字符、`ScopedCom`、`ScopedDpiAwareness`、显示器枚举、窗口可见边框、QPC
- [x] 3.3 `screen_grabbers`：DIB 画布、GDI、DXGI 复制（按桌面图像格式判定可用）、光标与点击光圈
- [x] 3.4 `WinScreenSource`：目标解析、取偶、DXGI→GDI 回退、窗口跟随、1ms 计时器精度
- [x] 3.5 `WasapiSource`（环回/采集、混音格式映射、QPC 年龄、设备失效）与 `audio_endpoints`
- [x] 3.6 `MfCameraSource`（模式选择、RGB32 输出、带符号行距、粘滞中断）与 `media_foundation`
- [x] 3.7 `CaptureDevices` 平台实现与 `VideoEncoders()` 探测
- [x] 3.8 CMake：Windows 系统库、`NOMINMAX`/`WIN32_LEAN_AND_MEAN`、非 Windows 排除 `capture/win/`

## 4. 节点

- [x] 4.1 `LiveSourceNode`：设备线程、冻结到 Start 的时钟、暂停丢弃、kEndOfStream→EOS、中断
- [x] 4.2 `ScreenCaptureNode`（恒定时隙、落后跳过计丢帧）
- [x] 4.3 `AudioCaptureNode`（10ms 轮询、QPC 时间戳、每秒重连）
- [x] 4.4 `CameraCaptureNode`（每秒重连、按帧尺寸）
- [x] 4.5 `AudioMixTimeline` 与 `AudioMixerNode`（覆盖或 150ms 截止出块、冲刷到结束时刻、增益/电平）
- [x] 4.6 `VideoCompositorNode`（取最新叠加帧、缩放镜像描边、1 秒过期隐藏）
- [x] 4.7 `FrameCallbackSinkNode`（RGBA 预览帧回调）
- [x] 4.8 `pixel_ops`：`BlitBgra`、`OutlineRectBgra`、`BlendDiscBgra`
- [x] 4.9 EncoderNode：RGB 源 BT.709 转换与标记；硬件编码器恒定质量映射

## 5. Facade

- [x] 5.1 `Recorder`：建图、可选源剔除重建、实时编码参数、暂停/音量、停止超时、单次完成、统计、来源
- [x] 5.2 `CameraPreview`

## 6. 应用层

- [x] 6.1 `shared_ui`（图标、分节标题、表单样式、滚轮保护、格式化、排除捕获），转码页/播放页/导航栏改用
- [x] 6.2 `desktop_mapping`（屏幕与显示器按位置匹配，物理↔逻辑坐标）
- [x] 6.3 `RegionSelector`（冻结截图覆盖层、拖动框选、单击选窗口）
- [x] 6.4 `RegionFrame`、`RecordingToolbar`、`CountdownOverlay`（排除捕获）
- [x] 6.5 `GlobalHotkeys`（Ctrl+Shift+F9/F10）
- [x] 6.6 `RecordingController`（倒计时→录制→辅助窗口→完成，回调切回 GUI 线程）
- [x] 6.7 `CaptureAreaSection`、`AudioSourceRow`+`LevelMeter`、`CameraSection`（预览）
- [x] 6.8 `RecorderPage`（表单、持久化、编码器后台探测、状态与电平、缺失来源提示、最近录制）
- [x] 6.9 导航栏「录屏」项、`recorder.svg`、主页卡片、`PlayerPage::OpenFile` 与主窗口路由

## 7. 测试与验证

- [x] 7.1 单元测试：BGRA 像素操作、混音时间线（抖动、空洞/重叠、迟到、增益/削波/峰值、无数据输入）
- [x] 7.2 集成测试：设备枚举、编码器探测、GDI/DXGI 区域录制、统计、暂停、静音环回、麦克风混音、摄像头叠加、可选源降级、空区域
- [x] 7.3 全部测试通过（58 项通过，2 项因无摄像头/未配置真实素材跳过）
- [x] 7.4 GUI 冒烟：全屏+系统声音+暂停+热键停止（2560x1440 DXGI + NVENC）、框选 1000x600 区域录制、辅助窗口均带 `WDA_EXCLUDEFROMCAPTURE`、最近录制双击在播放器播放
