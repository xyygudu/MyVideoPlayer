## Why

一次全面的代码审查找出了一批运行期缺陷：节点与应用层之间的事件回调跨线程重入、Link::Abort 丢失唤醒导致 Stop 偶发挂起、TS 时间戳不从 0 开始、低帧率（<10fps）视频跑得比音频快、转码在 Opus/格式中途变化/时间戳跳变时失败或产出损坏文件等。随后的使用又暴露出三个体验问题：

1. 暂停状态下修改 transform/颜色特效参数，要 seek 一下才能看到效果；
2. 特效参数没有"重置为默认值"的入口；
3. GPU 机器上无法切换到 CPU 解码/CPU 特效来对照验证——缺少调试开关。

同时仓库没有任何自动化测试，所有修复只能靠手工点 UI 验证，回归无从发现。

## What Changes

- **节点→应用层上报改为消息总线**：`graph/message_bus.h` 的 `GraphMessage{kEos, kError, kDeviceLost, kFirstFrame}` 由节点线程投递、facade 的事件线程消费；删除 `GraphEvent`/`SetEventCallback`/`GraphState` **BREAKING（内部接口）**
- **播放状态归 MediaPlayer 所有**：`PlaybackState` 由一把互斥锁保护，事件线程按 seek 世代统计各 sink 的 EOS；完成回调在事件线程、锁外调用
- **暂停时特效即时生效**：参数变化在暂停/播放结束时对屏幕上的帧做一次 refresh seek（mpv 做法），解码器丢弃目标之前的帧，同一帧经新特效链重新显示；连续拖动合并为一次后续刷新
- **特效重置**：`IEffectNode::ResetParams()` → `EffectManager::ResetParams()` → `MediaPlayer::ResetEffectParams()`；特效面板每组新增"重置参数"按钮
- **播放选项与诊断**：`PlayerOptions{hardware_decode, gpu_effects, zero_copy}`、`SetOptions()` 在当前位置重建管线并保持播放/暂停状态与特效设置；`GetPipelineInfo()`、`CaptureFrame()`（按帧色彩信息转换为 RGBA）
- **设置页**：导航栏底部新增"设置"，提供解码/特效/呈现开关（QSettings 持久化）、管线诊断、日志级别/控制台/日志目录、当前帧截图
- **GPU 设备丢失恢复**：VideoSinkNode 检测到设备丢失时投递 `kDeviceLost`，事件线程在新设备上重建管线（10 秒内最多 3 次）
- **低帧率同步**：同步偏差改用 ffplay 的"视频时钟 − 主时钟"，长帧一次性等完整个间隔（等待按 0.1 秒分片，但不再封顶）
- **时间戳**：DemuxNode 按容器 start_time 把时间轴平移到 0；转码时折叠 TS 时间戳跳变（ffmpeg 命令行语义），播放保持原始时间戳（ffplay 语义）
- **转码健壮性**：`TranscodeResult{kSucceeded, kCancelled, kFailed}` 取代 `bool`；编码器按能力协商采样率（Opus 44.1k→48k）、源尺寸/采样格式中途变化时缩放/重采样；视频 pts 不递增的帧按 ffmpeg vfr 语义丢弃
- **变换特效几何**：像素中心坐标下中心为 (n−1)/2，镜像精确映射到 n−1−x；只有精确的四分之一圈才走置换；NV12 置换两个色度分量都处理、双线性路径使用色度平面自己的映射——CPU 与 GPU 输出一致
- **日志**：`logging::SetLevel/GetLevel`（同时作用于 FFmpeg 桥接日志）、`IsConsoleLoggingEnabled`、`FileLogPath`
- **构建与测试**：核心库编成 OBJECT 库，DLL 与测试共用；`export.h` 改为生成到构建目录；新增 GoogleTest 测试套件，测试素材由 ffmpeg 在构建期生成，可选对真实视频目录跑播放测试

## Capabilities

### New Capabilities
- `settings-ui`：设置页（管线开关、诊断、日志、截图、持久化）
- `test-suite`：自动化测试套件与测试素材生成

### Modified Capabilities
- `media-graph-core`：事件回调与 GraphState 换成消息总线
- `player-state-machine`：播放状态由 MediaPlayer 持有
- `eof-propagation`：EOS 经消息总线按世代汇总
- `playback-control`：暂停、完成回调语义更新；新增选项、诊断、截图、暂停刷新、特效重置、设备丢失恢复
- `frame-timer-sync`：偏差定义与长帧等待
- `graph-transcode`：完成结果三态；时间戳折叠、pts 单调、格式变化适配
- `graph-source-nodes`：时间轴平移到 0、可选的时间戳跳变折叠
- `pixel-ops`：仿射映射中心与置换条件
- `effect-panel-ui`：重置按钮
- `app-shell-ui`：设置导航项
- `transcoder-ui`：完成状态三态
- `logging`：运行期日志级别

## Impact

- 公共头文件：`media_player.h`（`PlayerOptions`、`PipelineInfo`、`CapturedFrame` 及对应方法）、`transcoder.h`（`TranscodeResult`）、`logging.h`（`Level` 等）；`include/mvp/export.h` 移出源码树
- 代码：`src/media/`（message_bus、media_player 拆分为 `media_player.cc`/`media_player_pipeline.cc`/`media_player_impl.h`、frame_capture、audio_resampler、各节点、pixel_ops、affine 着色器）、`src/app/`（settings_page、navigation_bar、main_window、effect_panel）
- 构建：`mvp_core_objects` OBJECT 库；`MVP_BUILD_TESTS`（默认 ON，配置期下载 GoogleTest v1.15.2，需要 FFmpeg 自带的 ffmpeg.exe 生成素材）
- 不在本期：AV1 优先选用原生解码器以启用硬解、10-bit 特效、4:2:2 方形画面旋转 90° 的色度置换
