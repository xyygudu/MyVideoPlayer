## Context

MediaGraph 至今只承载文件源：DemuxNode 读文件、按 pts 推数据，时钟由 sink 驱动。录屏需要的是"实时源"：数据按真实时间产生，不能回压到设备；多个源（屏幕、系统声音、麦克风、摄像头）必须落在同一条时间轴；用户可以暂停；结束由用户触发而不是读到文件尾。facade 层已有 `MediaPlayer`、`Transcoder` 两个先例（各自持有图、事件线程消费 MessageBus），Recorder 沿用同一模式。

## Goals / Non-Goals

**Goals:**
- 在现有节点/端口/链路/消息总线框架内表达实时采集，不为录屏另起一套管线
- 屏幕（全屏/区域/窗口）、系统声音、麦克风、摄像头画中画、鼠标指针；音画同步；暂停时长不计入
- 设备问题（打不开、中途拔出）不让整次录制失败
- 平台相关代码集中在 `capture/win/`，上层只依赖接口

**Non-Goals:**
- Windows Graphics Capture（被遮挡窗口、跨显示器窗口的独立捕获）、HDR 桌面、多显示器拼接
- 设备时钟漂移的重采样校正、跟随系统默认设备切换（IMMNotificationClient）
- 直播推流、场景编辑（多源自由排版）

## Decisions

### D1 采集后端：平台原生 API 封装在接口之后
三种主流做法差异很大：

| | 屏幕 | 系统声音 | 摄像头 | 时间戳 |
|---|---|---|---|---|
| ffmpeg CLI | gdigrab / ddagrab（lavfi） | 无 WASAPI 环回输入，需虚拟声卡 | dshow | 各设备各自 wallclock |
| OBS | win-capture：DXGI 复制 / WGC / BitBlt | win-wasapi（环回） | win-dshow | 统一 os_gettime_ns |
| GStreamer | d3d11screencapturesrc | wasapi2src loopback=true | mfvideosrc | 管线 running-time |
| 本项目 | DXGI 复制 + GDI 回退 | WASAPI 环回 / 采集 | MF Source Reader | 图的主时钟 |

不用 ffmpeg 的设备 demuxer：拿不到环回声音，各设备时间戳无法统一，光标/点击高亮也无从插手。按 OBS/GStreamer 的做法，每类设备一个接口（`ScreenSource/AudioSource/CameraSource`），Windows 实现放在 `capture/win/`，由工厂函数创建；节点只依赖接口。

### D2 实时源基类 LiveSourceNode（对照 GstBaseSrc 的 live 模式）
- **设备线程**：COM 套间、DXGI 复制、线程级 DPI 感知都与线程绑定，因此 Open 时启动设备线程、在该线程打开设备（格式在协商前确定，满足"Open 获取设备、Negotiate 纯推理"的既有约束），Start 后在同一线程采集，退出前在同一线程关闭。
- **结束**：`CommandType::kEndOfStream` 让设备线程停止采集并推 EOS（GStreamer `send_event(EOS)` 的语义），编码器冲刷、muxer 写尾后经总线上报 kEos；`Stop()` 则是中止，不推 EOS。
- **派生类只实现钩子**：`OpenDevice / CaptureStep / CloseDevice / InterruptDevice`，节拍、重连各自决定。

### D3 时间轴：主时钟即录制的 running-time
每个实时源 offer 一个 `Clock`（优先级 50，高于无时钟节点、相互等价），Prepare 时冻结在 0、Start 时解冻；所有源都用**图仲裁出的主时钟**打时间戳，于是多源天然同轴。暂停直接用 `MediaGraph::SetPaused`：时钟冻结、源丢弃采集到的数据，恢复后时间接着走——暂停时长不进入文件。GStreamer 的 running-time 在 PAUSED 时同样不前进；OBS 是暂停时记录时长并在输出端平移时间戳，效果相同但需要额外的偏移簿记；ffmpeg CLI 没有暂停。
- 音频包以设备 QPC 时间戳推算"首样本采集时刻"（`Now − age`），而不是读取时刻，消除轮询带来的 10ms 级抖动。

### D4 视频节拍：恒定时隙，落后则跳过（输出 VFR）
第 n 帧应在 n/fps 采集，pts 就是 n/fps。采集或下游回压导致落后时，直接跳到当前时隙并把跳过的计为丢帧，时间戳保留空洞。OBS 在渲染循环落后时复制帧保持 CFR（"渲染延迟导致丢帧"统计）；gdigrab 按 wallclock 睡眠、落后时连续抓取。我们选择跳过：不制造重复帧、不追赶突发，音画由时间戳对齐，MP4/MKV 都支持变帧率。

### D5 光标在屏幕源内绘制
与 OBS、gdigrab/ddagrab 的 `draw_mouse` 一致，`CursorPainter` 在 DIB 画布上 `DrawIconEx`（热点按 `GetIconInfo` 缓存），按键按下时先画半透明光圈（`pixel_ops::BlendDiscBgra`）。不做成单独的叠加节点：光标位置与抓图必须同一时刻取得。

### D6 音频混合：时间戳对齐 + 延迟截止（对照 GStreamer audiomixer/aggregator）
- `AudioMixTimeline` 是纯数据结构（可单测）：每路输入从读位置起保存样本；写入时与上次结束位置相差不超过容差（20ms）就首尾相接，超过则留空洞（读出为静音）或丢弃重叠，早于读位置的样本丢弃。
- `AudioMixerNode` 每 5ms 非阻塞拉取所有输入（`InputPort::TryPull`），每路先经共享 `AudioResampler` 转为 48kHz 立体声 float planar，按 1024 样本（一个 AAC 帧）出块。出块条件：所有仍在运行的输入都覆盖了该块，**或**主时钟已超过块末 150ms。后者是必须的——WASAPI 环回在系统静音时完全不送包，若等它就会卡死整条音频。GStreamer aggregator 对实时源也是"超时（latency）到了就用已有数据出块"；OBS 用固定缓冲时长混音。
- 所有输入 EOS 后冲刷到"已写入的末尾"与"结束时刻的主时钟"中较大者，保证静音环回的音轨与视频等长。

### D7 摄像头画中画：主输入定节奏，叠加输入取最新
`VideoCompositorNode` 必须是 Active：Passive 节点的 `Process()` 无法区分数据来自哪个输入端口。主输入（屏幕）阻塞拉取决定输出节奏，叠加输入（摄像头）每帧前非阻塞抽干只留最新一帧，缩放一次后每帧贴图；最新帧超过 1 秒未更新（摄像头拔出、被占用）就隐藏而不是冻结在画面上。对照：OBS 的异步源按时间戳取最近帧；ffmpeg overlay 滤镜 `eof_action=pass`；GStreamer compositor 在实时模式下同样按 latency 截止。摄像头链路深度 4，确保摄像头帧率高于屏幕帧率时设备线程也不被阻塞。

### D8 可选源降级：重建而非热插拔
节点能力在协商期定型，图建成后不支持删节点。因此 `Recorder::Start` 中 `graph.Open()` 失败时检查失败节点（`State()==kError`）：若是摄像头/麦克风/系统声音，就去掉它重建再试；屏幕或输出失败则整体失败。`Sources()` 告诉 UI 实际录了什么。运行中的设备丢失由节点自己重连，期间画面隐藏叠加层、音频以静音填补——不打断录制（OBS 的行为）。

### D9 编码参数：实时优先
- `CaptureDevices::VideoEncoders()` 真正打开一次编码会话来判断可用（编进 FFmpeg 不代表有 GPU/驱动），硬件编码器排在前面，UI 默认选第一个。
- Recorder 只取用户的编码器与画质（CRF 值），预设/GOP/B 帧由它决定：x264 `veryfast`、NVENC `p4`、QSV `veryfast`（OBS 默认值），GOP 2 秒，硬件编码器不开 B 帧。
- EncoderNode 的 CRF 模式映射到各硬件编码器的恒定质量参数（NVENC `rc=vbr + cq + bit_rate=0`、QSV `global_quality`、AMF `rc=cqp + qp_i/p/b`），对转码同样有效。
- RGB 输入（屏幕）按 BT.709 有限范围转 YUV 并写入色彩标记（OBS 默认的 Rec.709）；swscale 默认 BT.601，未标记的流会让播放器猜错。

### D10 屏幕上的辅助窗口不进录像
倒计时、录制区域边框、悬浮工具条都用 `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` 排除出屏幕捕获（DXGI 复制与 GDI 都生效，Windows 10 2004+），边框还画在区域外侧、鼠标穿透。区域框选时主窗口先隐藏，覆盖层显示冻结的截图（ShareX/Snipaste 做法）；单击选取光标下最上层窗口。
- Qt 6 的 `QScreen::name()` 在 Windows 上是显示器型号而非 GDI 设备名，屏幕与显示器按位置匹配：Qt 保持每块屏幕原点为物理坐标、只缩放尺寸（`QHighDpi::fromNativeScreenGeometry`），混合 DPI 下逐屏换算。
- 全局热键 Ctrl+Shift+F9（开始/停止）与 Ctrl+Shift+F10（暂停/继续）：`RegisterHotKey` 线程热键 + Qt 原生事件过滤器；避开 Alt+F9（NVIDIA ShadowPlay）与 Ctrl+Shift+R 等常被占用的组合。

### D11 UI 公共件
SVG 图标加载在导航栏、播放页、转码页各有一份，录屏页和工具条还要用（白色图标），于是抽出 `shared_ui`（`LoadSvgIcon`、`MakeSectionHeader`、`FormStyleSheet`、`GuardWheel`、格式化、`ExcludeFromCapture`）。文件名不能用 `ui_` 前缀：Qt AUTOUIC 会把 `#include "ui_xxx.h"` 当作 uic 生成头去找 `xxx.ui`。长表单页用 `GuardWheel` 让下拉框/滑块在未获焦点时把滚轮让给滚动区，避免滚动页面时误改设置。

## Risks / Trade-offs

- [设备时钟与 QPC 存在漂移（典型 50ppm），长时间录制累积超过 20ms 容差时会插入一小段静音或丢弃一小段] → 记录为改进项：按时间戳偏差做异步重采样（类似 OBS 的时间戳平滑 / GStreamer audiobasesrc 的 slaving）
- [BGRA→YUV 在 CPU 上转换，4K60 接近单核上限] → 改进项：D3D11 视频处理器转 NV12 并直送硬件编码器
- [窗口模式用 GDI 抓窗口所在屏幕区域，被遮挡时录到的是遮挡物；窗口尺寸以开始时为准] → 改进项：Windows Graphics Capture
- [DXGI 复制只覆盖单块显示器，区域跨屏时退回 GDI] → 可接受，日志说明
- [摄像头预览在 GUI 线程同步打开设备，部分摄像头会卡顿约 1 秒] → 改进项：后台线程打开
- [MP4 在进程崩溃时没有 moov 无法播放] → UI 提供 MKV 选项并说明；改进项：分段 MP4
- [应用层 spdlog 与 mvp_core 各自一份注册表，UI 日志只到控制台] → 既有问题，录制相关日志都在核心库内
