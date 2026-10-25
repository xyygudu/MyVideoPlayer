# 录屏待改进点

> 记录时间：2026-10-25
> 对标参考：OBS Studio（libobs audio/video 管线、win-capture、win-wasapi、obs-nvenc）、GStreamer（audiobasesrc、d3d11screencapturesrc、wasapi2src）、FFmpeg（ddagrab、movenc）
> 背景：change `add-screen-recorder` 实现了 DXGI/GDI 屏幕采集、WASAPI 环回与麦克风、Media Foundation 摄像头、按时间戳混音与画中画合成

---

## 1. 音频设备时钟漂移只靠容差吸收

### 问题

`AudioMixTimeline` 以 20ms 容差把相邻音频包首尾相接，包的时间戳来自设备 QPC。声卡晶振与系统时钟存在漂移（典型 ±50ppm），相接的样本数与真实时间逐渐偏离，累计超过容差后时间线会插入一段静音（设备偏慢）或丢弃一段重叠（设备偏快）。

### 影响场景

- **长时间录制**：50ppm 时约 7 分钟累积 20ms，之后每隔几分钟出现一次约 20ms 的静音或跳音
- **两路声音同录**：系统声音与麦克风各自漂移，混音中两者的相对位置也会慢慢错开

### 改进建议（参考 GStreamer audiobasesrc 的 slave-method=resample、OBS 的时间戳平滑）

为每路输入估计"时间戳推进量 / 样本推进量"的比值（滑动窗口线性回归），用 `swr_set_compensation()` 以极小的比例补偿采样率，让写入位置始终贴合时间戳，而不是在超出容差时跳变：

```cpp
// 每秒更新一次
double drift = (pts_now - pts_start) * 48000.0 / samples_written - 1.0;  // 例如 +4e-5
swr_set_compensation(swr, static_cast<int>(drift * 48000), 48000);       // 每 48000 样本补偿 N 个
```

---

## 2. 窗口录制抓的是屏幕区域，不是窗口本身

### 问题

窗口模式按窗口当前位置从屏幕上抓取（GDI），尺寸固定为开始时的大小。窗口被其他窗口遮挡时录到遮挡物，最小化时画面冻结，窗口拖到 DXGI 不能覆盖的跨屏位置时只能用 GDI。

### 影响场景

- **边录边切到别的程序**：录像里出现别的程序的画面
- **窗口改变大小**：超出部分被裁掉或留黑

### 改进建议（参考 OBS win-capture 的 Windows Graphics Capture 模式、GStreamer d3d11screencapturesrc `capture-api=wgc`）

Windows 10 1903+ 用 `Windows.Graphics.Capture`（`GraphicsCaptureItem` 由 `IGraphicsCaptureItemInterop::CreateForWindow` 创建）直接获取窗口内容的 D3D11 纹理，不受遮挡影响；新增 `WgcScreenSource` 实现 `ScreenSource` 接口，按帧尺寸变化缩放到固定输出尺寸，节点与 facade 不变。

---

## 3. BGRA→YUV 在 CPU 上转换

### 问题

屏幕帧以 BGRA 进入 EncoderNode，由 swscale 在 CPU 上转为 YUV420P 再交给编码器；DXGI 路径还要先把桌面纹理拷到 staging 再读回内存。即使用 NVENC，也付出了读回 + 转换的 CPU 成本。

### 影响场景

- **4K60 录制**：单核转换接近 16ms/帧，开始丢帧
- **游戏录制**：CPU 被占用，影响前台程序

### 改进建议（参考 OBS 的纹理编码（obs-nvenc 直接接收 D3D11 纹理）、FFmpeg `ddagrab` → `hwupload`/`scale_d3d11`）

DXGI 复制得到的纹理在 GPU 上用 D3D11 视频处理器（`ID3D11VideoProcessor`，BGRA→NV12，同时做缩放）转换，输出硬件帧（`PixelFormat::kD3D11`），编码器以 `hw_frames_ctx` 打开 NVENC/QSV/AMF 直接编码；软件编码器才在节点边界下载。项目已有图级 `GpuDevice` 与硬件帧域，可复用。

---

## 4. 不跟随系统默认音频设备的切换

### 问题

设备 id 为空时只在打开时取一次默认设备。录制中用户在系统里把默认输出从扬声器切到耳机，环回仍然录原来的设备（此时它已经没有声音）；只有设备失效（拔出）才会触发重连并重新取默认设备。

### 影响场景

- **录制中插上蓝牙耳机**：Windows 切换默认设备，录像里之后的系统声音全部消失

### 改进建议（参考 OBS win-wasapi 对 "Default" 设备注册 `IMMNotificationClient`）

`WasapiSource` 在默认设备模式下注册 `IMMNotificationClient::OnDefaultDeviceChanged`，回调里只置标志，设备线程下一次 `Read()` 返回"需要重开"，`AudioCaptureNode` 复用已有的重连路径。

---

## 5. 摄像头预览在 GUI 线程同步打开设备

### 问题

`CameraPreview::Start()` 同步创建 Media Foundation 设备源、Source Reader 并设置媒体类型，部分摄像头需要 0.5~1 秒，期间界面无响应（勾选"叠加摄像头"或切到录屏页时）。

### 影响场景

- **USB 摄像头首次打开**：界面卡顿约 1 秒
- **摄像头被占用**：卡顿后才显示"无法打开摄像头"

### 改进建议（参考 OBS 在源自己的线程里激活设备、Qt 的异步模式）

`CameraSection` 在 `QThread` 中调用 `Start()`，完成后通过排队调用回到 GUI 线程更新预览状态；或给 `CameraPreview` 增加 `StartAsync(callback)`。注意 `Stop()` 必须等待正在进行的打开完成。

---

## 6. MP4 录像在进程崩溃时不可播放

### 问题

MP4 的 moov 在写尾时才写入，录制中程序崩溃或断电会留下无法播放的文件。目前只在界面上提示 MKV 可以保留已录内容。

### 影响场景

- **长时间录制中崩溃/断电**：整段 MP4 丢失

### 改进建议（参考 OBS "录制为 MKV，结束后自动重封装为 MP4"、FFmpeg movenc `movflags=frag_keyframe+empty_moov`）

方案 A：MuxNode 支持容器选项，录屏的 MP4 使用分段 MP4（每个关键帧一个 fragment），崩溃时最多丢最后一个 GOP。方案 B：始终录 MKV，完成后在后台用 remux 图（Demux→Mux，不解码）转成 MP4。
