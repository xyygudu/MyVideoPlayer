## Why

工具箱已有播放器和转码器，但缺少最常用的另一类工具——录屏：录制全屏、窗口或框选区域，同时录下电脑正在播放的声音和麦克风，可选叠加摄像头画面，鼠标指针也要录进去。这正好检验 MediaGraph 能否承载"实时源"：此前所有源都是文件（DemuxNode），图里没有设备线程、实时时钟、多路输入混合，也没有"让实时源主动结束"的手段。

## What Changes

- **实时采集源节点**：`LiveSourceNode` 基类（设备线程在 Open 时打开设备以确定格式、Start 后采集、`kEndOfStream` 命令收尾推 EOS；每个源提供冻结在 0 直到 Start 的时钟，所有源按主时钟打时间戳；暂停期间丢弃采集数据），派生 `ScreenCaptureNode`（恒定帧率时隙、落后跳过并计为丢帧、光标绘制）、`AudioCaptureNode`（WASAPI 环回/麦克风，按设备 QPC 时间戳打时间戳，设备丢失每秒重连）、`CameraCaptureNode`（Media Foundation，失败每秒重连）
- **平台采集后端**：`capture::ScreenSource/AudioSource/CameraSource` 接口 + Windows 实现（DXGI 桌面复制 + GDI 回退、`DrawIconEx` 光标与点击高亮、WASAPI、Media Foundation Source Reader），工厂函数隔离平台
- **混音与合成**：`AudioMixTimeline`（按时间戳摆放、抖动吸收、空洞补静音、重叠/迟到丢弃、增益、削波、峰值）与 `AudioMixerNode`（48kHz 立体声 1024 样本块，全覆盖或超过 150ms 延迟即出块，全部输入结束后冲刷到结束时刻再发 EOS）；`VideoCompositorNode`（主输入定节奏，叠加输入非阻塞取最新帧，缩放/镜像/描边，叠加源 1 秒无新帧则隐藏）
- **录屏 facade**：`mvp::Recorder`（Start/Pause/Resume/Stop/Stats/Sources/SetVolume/完成回调；打不开的可选源自动剔除后重建；停止 15 秒超时；暂停时长不计入）、`mvp::CameraPreview`、`mvp::CaptureDevices`（显示器/窗口/摄像头/音频设备枚举、可用 H.264 编码器探测，硬件优先）
- **图引擎扩展**：`CommandType::kEndOfStream`；`InputPort::TryPull`/`Link::TryPop` 非阻塞取数；`MessageBus::WaitFor/IsShutDown`
- **编码器**：RGB 源转 BT.709 有限范围 YUV 并写色彩标记；CRF 模式对硬件编码器映射为各自的恒定质量参数（NVENC cq、QSV global_quality、AMF cqp）
- **像素工具**：`pixel_ops` 新增 BGRA 贴图（裁剪/镜像）、矩形描边、抗锯齿圆形混合
- **应用层**：「录屏」页（录制区域、画面、声音及电平、摄像头及预览、输出、最近录制）、屏幕框选器（冻结截图、拖拽框选、单击选窗口、混合 DPI）、录制区域边框、悬浮工具条、倒计时（三者都排除出屏幕捕获）、全局热键 Ctrl+Shift+F9/F10；导航栏与主页新增入口；录制完成可直接在播放器打开
- **UI 公共件**：页面重复的 SVG 图标加载、分节标题、表单样式表抽到 `shared_ui`，转码/播放/导航栏改用

## Capabilities

### New Capabilities
- `live-capture-sources`: 实时采集源节点（屏幕、音频设备、摄像头）、平台采集后端与设备枚举
- `live-av-mixing`: 多路实时音频按时间戳混音、画中画视频合成
- `screen-recorder`: `Recorder`/`CameraPreview` facade：建图、可选源降级、暂停、停止收尾、统计
- `recorder-ui`: 录屏页、区域框选、录制中屏幕辅助窗口、全局热键、最近录制

### Modified Capabilities
- `graph-command-control`: `CommandType` 新增 `kEndOfStream`
- `media-graph-core`: 非阻塞取数 `InputPort::TryPull`；`MessageBus::WaitFor`
- `graph-transform-nodes`: EncoderNode 的 RGB 色彩转换与硬件编码器恒定质量映射
- `pixel-ops`: BGRA 绘制原语
- `app-shell-ui`: 「录屏」导航项
- `home-dashboard-ui`: 「录屏」卡片

## Impact

- 公共头文件：新增 `capture_devices.h`、`recorder.h`、`camera_preview.h`、`captured_frame.h`（`CapturedFrame` 从 `media_player.h` 移出，后者改为包含它）
- 代码：`src/media/capture/`（接口）与 `src/media/capture/win/`（Windows 实现）、`src/media/nodes/` 新增 6 个节点、`audio_mix_timeline`、`recorder.cc`、`camera_preview.cc`；`src/app/` 新增录屏页及其组件、`shared_ui`
- 构建：Windows 下 `mvp_core` 链接 `d3d11 dxgi dwmapi winmm ole32 mfplat mf mfreadwrite mfuuid`；`mvp_core_objects` 统一定义 `NOMINMAX WIN32_LEAN_AND_MEAN`；非 Windows 平台不编译 `capture/win/`
- 测试：BGRA 像素操作、混音时间线、设备枚举、录屏集成测试（GDI/DXGI、暂停、静音环回、麦克风、摄像头、可选源降级、空区域）
- 不在本期：Windows Graphics Capture 窗口捕获（被遮挡窗口）、HDR 桌面、音频时钟漂移重采样、跟随系统默认设备切换、GPU 色彩转换直送硬件编码器、分段 MP4 防崩溃
