## MODIFIED Requirements

### Requirement: Play starts graph execution
Play() SHALL 按当前状态动作：kReady 时经 `MediaGraph::Start()` 级联启动所有节点并进入 kPlaying；kPaused 时恢复图与时钟；kFinished 时先 seek 到 0 再播放（重播）；其余状态忽略。

#### Scenario: Play after Open
- **WHEN** `Open()` 成功后调用 Play()
- **THEN** 状态为 kPlaying，帧开始呈现

#### Scenario: Play after the end replays
- **WHEN** 状态为 kFinished 时调用 Play()
- **THEN** 从头开始播放，状态为 kPlaying

### Requirement: Seek flushes graph and repositions
Seek() SHALL 调用 `MediaGraph::Seek(pos)`（Flush → 全部时钟在新世代重置到目标 → 广播 kSeek），并清除已收集的 EOS 与待补的暂停刷新。kIdle/kError 时 SHALL 忽略；kFinished 时 SHALL 转为 kPaused，暂停在目标位置。

#### Scenario: Seek while paused shows the target frame
- **WHEN** 暂停时调用 Seek(3.0)
- **THEN** 状态保持 kPaused，屏幕显示 3 秒处的帧

### Requirement: Duration and position queries
系统 SHALL 提供 Duration()（探测得到的源时长）/ CurrentPosition()（主时钟读数）/ VideoFps() / State() 查询接口，均可在任意线程调用。

#### Scenario: Position follows playback
- **WHEN** 播放 1 秒后查询
- **THEN** `CurrentPosition()` 约为 1.0，`Duration()` 为源时长

### Requirement: Close releases resources
Close() SHALL 停止事件线程、经 `MediaGraph::Stop()` 停止并释放全部节点、关闭渲染器，状态回到 kIdle。

#### Scenario: Close then reopen
- **WHEN** 播放中调用 Close() 再 Open() 另一个文件
- **THEN** 旧管线完全释放，新文件正常播放

### Requirement: Pause pauses graph execution
Pause() 仅在 kPlaying 时生效：SHALL 经 `MediaGraph::SetPaused(true)` 暂停所有节点并冻结图内全部时钟，`PlaybackState` 转为 kPaused；屏幕保持当前帧。

#### Scenario: Pause keeps the frame on screen
- **WHEN** 播放中调用 Pause()
- **THEN** `State()` 为 kPaused，`CurrentPosition()` 不再推进，`CaptureFrame()` 返回的帧不变

### Requirement: Playback finished callback
SetPlaybackFinishedCallback 注册的回调 SHALL 在每次播放到结尾（状态转为 kFinished）时触发一次，在内部事件线程上执行（见 eof-propagation）。

#### Scenario: Replay reports again
- **WHEN** 播放结束后调用 Play() 重播并再次播完
- **THEN** 回调共被调用两次

## ADDED Requirements

### Requirement: 播放选项在运行期切换管线
系统 SHALL 提供 `PlayerOptions{hardware_decode, gpu_effects, zero_copy}`（默认全开）与 `MediaPlayer::SetOptions()` / `Options()`：
- `hardware_decode`：关闭时视频解码器只输出软件帧
- `gpu_effects`：关闭时特效节点不创建 GPU pass，硬件帧下载后在 CPU 上处理
- `zero_copy`：关闭时渲染器不绑定硬件纹理，先下载再上传

未打开源时选项只被记录，下一次 `Open()` 生效。已打开时 SHALL 立即在屏幕帧位置重建整条管线，并保持播放/暂停状态与所有特效的启用状态和参数。

#### Scenario: 切换到软件解码保持位置与状态
- **WHEN** 暂停在 1.48s 时调用 `SetOptions({hardware_decode=false})`
- **THEN** `GetPipelineInfo().hardware_decoding` 为 false，状态仍为 kPaused，屏幕帧 pts 仍为 1.48s，特效参数不变

#### Scenario: 未打开时只记录
- **WHEN** `Open()` 之前调用 `SetOptions`
- **THEN** 不建图，随后的 `Open()` 按该选项建图

### Requirement: 管线诊断信息
系统 SHALL 提供 `MediaPlayer::GetPipelineInfo()`，返回 `PipelineInfo{video_codec, hardware_decoding, renderer, frames_presented}`：视频解码器名称、是否正在 GPU 解码、SDL 渲染后端名、自 `Open()` 以来已呈现的视频帧数。

#### Scenario: 反映实际解码路径
- **WHEN** 打开 H.264 文件且设备支持 D3D11VA
- **THEN** `video_codec` 为 "h264"、`hardware_decoding` 为 true、`renderer` 为 "direct3d11"

### Requirement: 截取屏幕帧
系统 SHALL 提供 `MediaPlayer::CaptureFrame()`，返回屏幕上的帧（经过特效之后）转换成的 8-bit RGBA（`CapturedFrame{width, height, pts, rgba}`），无帧时 `width == 0`。转换 SHALL 使用帧自身的矩阵与范围（未标注时按 mpv 规则猜测），硬件帧先下载。转换 SHALL 在 facade 锁之外进行。

#### Scenario: 色彩按源标注还原
- **WHEN** 截取一段以 BT.709 编码的纯色 0x3366CC（有限范围或全范围、或全范围 yuvj）
- **THEN** 中心像素的 RGB 与 0x3366CC 相差不超过 4

### Requirement: 暂停时特效修改即时显示
暂停或播放结束（kPaused/kFinished）时修改特效参数、启用状态或重置参数，系统 SHALL 让屏幕上的同一帧经新的特效链重新显示（refresh seek：seek 到屏幕帧的 pts，解码器丢弃更早的帧）。同一时刻 SHALL 至多一次刷新在途；刷新上屏（`kFirstFrame`）之前的后续修改 SHALL 合并为一次补充刷新。用户 seek SHALL 取消待补刷新。播放中的修改无需刷新。

#### Scenario: 暂停时调亮度
- **WHEN** 暂停后调用 `SetEffectParam("color", "brightness", 0.4f)`
- **THEN** 无需 seek，屏幕帧变亮，其 pts 与暂停时相同，状态仍为 kPaused

#### Scenario: 暂停时水平翻转
- **WHEN** 暂停后调用 `SetEffectParam("transform", "flip_h", true)`
- **THEN** 屏幕帧变为暂停帧的精确镜像

#### Scenario: 连续拖动合并
- **WHEN** 刷新在途时又连续修改参数多次
- **THEN** 当前刷新上屏后只再刷新一次，使用最新参数

### Requirement: 特效参数重置
系统 SHALL 提供 `MediaPlayer::ResetEffectParams(effect_id)`，将该特效的所有参数恢复为各自的 `default_value`，启用状态保持不变；未知 `effect_id` SHALL 记录警告并忽略。暂停时同样即时刷新。

#### Scenario: 重置颜色特效
- **WHEN** 修改亮度与对比度后调用 `ResetEffectParams("color")`
- **THEN** `EffectInfos()` 中 color 的每个参数 `value == default_value`，屏幕帧恢复原样

### Requirement: GPU 设备丢失时重建管线
VideoSinkNode 检测到渲染设备丢失时 SHALL 投递 `kDeviceLost`。MediaPlayer 的事件线程 SHALL 让渲染器重新包装 SDL 重建的设备，并在当前位置重建管线（保持状态与特效）；10 秒内超过 3 次 SHALL 放弃并进入 kError。

#### Scenario: 驱动重置后继续播放
- **WHEN** 播放中显卡驱动重置导致设备丢失
- **THEN** 管线在新设备上重建，从丢失时的位置继续播放

## REMOVED Requirements

### Requirement: StepFrame advances one frame while paused
**Reason**: `MediaPlayer` 没有 StepFrame 接口；暂停时的单帧显示由 seek 预览（VideoSinkNode 的 preview epoch）承担。
**Migration**: 暂停时调用 `Seek(目标位置)` 显示指定帧。
