# live-av-mixing Specification

## Purpose
Defines how live media streams are combined inside the graph: the
timestamp-aligned audio mixer (AudioMixTimeline, AudioMixerNode) and the
picture-in-picture video compositor (VideoCompositorNode).
## Requirements
### Requirement: AudioMixTimeline 按时间戳摆放音频
系统 SHALL 提供纯数据结构 `AudioMixTimeline(inputs, channels, tolerance)`，位置以绝对样本序号表示。`Write(input, planes, count, start)` SHALL：若 `start` 与该输入上次写入的结束位置相差不超过 `tolerance`，则紧接上次结束位置写入；否则大于结束位置时留出空洞（读出为静音），小于时丢弃重叠部分；早于 `ReadPosition()` 的样本 SHALL 丢弃。`BufferedEnd(input)` SHALL 返回该输入写入的结束位置（无数据时等于读位置）。

#### Scenario: 时间戳抖动
- **WHEN** 第二段写入比上一段结束晚 5 个样本，容差为 10
- **THEN** 第二段紧接第一段，没有空洞

#### Scenario: 空洞与重叠
- **WHEN** 写入 [0,50)，再写 [100,150)，再写起点为 120 的 50 个样本
- **THEN** [50,100) 读出为 0，第三段只保留 [150,170)

#### Scenario: 迟到
- **WHEN** 读位置已到 100 后写入 [80,130)
- **THEN** 只保留 [100,130)

### Requirement: AudioMixTimeline 混音
`MixNext(out, count, gains, peaks)` SHALL 输出从读位置起 `count` 个样本：各输入乘以各自增益后相加（缺失样本按 0），结果限制在 [-1, 1]；`peaks[i]` SHALL 为输入 i 在本块内乘增益后的绝对值峰值；之后读位置前进 `count`。

#### Scenario: 增益与削波
- **WHEN** 两路分别为 0.9（增益 2）与 -0.2（增益 1）
- **THEN** 输出为 1.0

### Requirement: AudioMixerNode 混合实时音频
`AudioMixerNode(inputs)`（Transform、Active）SHALL 每 5ms 用 `InputPort::TryPull` 取走所有输入上排队的数据，经各自的 `AudioResampler` 转为 48kHz、立体声、float planar 后写入时间线（位置 = pts × 48000），并输出 1024 样本的块，块的 pts 为其起始位置 / 48000。一个块 SHALL 在以下任一条件满足时输出：所有未结束的输入都已覆盖到块末；或主时钟已超过块末 150ms。输入格式 SHALL 不受限制（采样率/声道/样本格式可中途变化）。

#### Scenario: 静音的环回设备不阻塞混音
- **WHEN** 系统没有声音播放，环回设备不送任何数据，同时麦克风正常
- **THEN** 混音仍按时输出，环回部分为静音，延迟不超过约 150ms

#### Scenario: 暂停不制造数据
- **WHEN** 图暂停 1 秒
- **THEN** 暂停期间没有新块输出，恢复后时间线连续

### Requirement: AudioMixerNode 结束与电平
所有输入都收到 EOS 后，混音器 SHALL 冲刷重采样器残留，输出到 max(各输入写入末尾, 结束时主时钟位置) 为止（最后一块补静音），然后推送 EOS。`SetGain(input, gain)`（0..4）与 `TakeLevel(input)`（返回上次调用以来的峰值并清零）SHALL 可在任意线程调用。

#### Scenario: 静音环回的音轨与视频等长
- **WHEN** 只录系统声音且整段无声，录制 2 秒后停止
- **THEN** 输出的 AAC 音轨约 2 秒

### Requirement: VideoCompositorNode 画中画合成
`VideoCompositorNode(CameraOverlayOptions)`（Transform、Active，两个 `kRGB32` 视频输入）SHALL 以主输入（输入 0）的帧决定输出节奏：每个主帧到达时，用 `TryPull` 取走叠加输入（输入 1）上排队的全部帧并只保留最新一帧，缩放到主画面宽度 × `width_ratio`（取偶、保持宽高比），按 `corner` 放在距边 max(8, 宽/50) 像素处，`mirror` 时水平镜像，外加 2 像素白色描边，然后输出主帧（时间戳、标志、世代不变）。叠加帧比主帧早 1 秒以上时 SHALL 不绘制。主输入 EOS 时 SHALL 转发 EOS 并结束。

#### Scenario: 摄像头在右下角
- **WHEN** 1280x720 屏幕叠加 640x480 摄像头，`width_ratio` 0.25、右下角
- **THEN** 摄像头画面为 320x240，右下角距边 25 像素

#### Scenario: 摄像头中途断开
- **WHEN** 摄像头停止送帧超过 1 秒
- **THEN** 画面中的摄像头窗口消失，屏幕画面照常输出

