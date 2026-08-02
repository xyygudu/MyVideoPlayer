## MODIFIED Requirements

### Requirement: VideoSinkNode renders video frames to display
系统 SHALL 定义 `VideoSinkNode`（Sink 类型），使用 SDL3 GPU 渲染逻辑输出到窗口。

VideoSinkNode SHALL 提供：
- 单个输入端口：接收 MediaBuffer（payload 为 MediaFrame）
- 渲染路径：D3D11 零拷贝 → NV12 → YUV420P → swscale fallback
- 时基：自持一个时钟并在每帧显示时更新，通过 `ProvideClock()` 参与主时钟仲裁（优先级低于音频）
- 同步逻辑：`Negotiate()` 取得 `MasterClock()`；主时钟非自身时按 frame_timer 累积算法收敛，主时钟为自身或为空时按帧间隔自由走时
- 当前帧：SHALL 保存最后一次显示的帧，供重绘使用
- `Negotiate()`：SHALL 校验输入端口已连接且为视频格式，并从 `input_port_->Format().AsVideo().frame_rate` 读取帧率；SHALL NOT 依赖外部 setter 注入帧率或时钟
- ThreadingMode：Active

**呈现与时基推进 SHALL 是两个独立动作**：呈现帧的函数 SHALL 只负责「保存为当前帧并渲染」，SHALL NOT 隐含推进播放位置。时基推进 SHALL 由调用点在呈现时刻显式执行，使「换一帧并显示」与「重显当前帧」形成语义对称的一对。

VideoSinkNode SHALL NOT 自行校验 buffer 世代 —— 该职责已归属输入端口。

#### Scenario: Render a YUV420P software frame
- **WHEN** 输入端口收到 YUV420P 的 MediaFrame
- **THEN** 使用 SDL_UpdateYUVTexture 上传并渲染

#### Scenario: Frame sync against master clock
- **WHEN** 主时钟由其他节点提供，video_pts 与主时钟差值在容差内
- **THEN** frame_timer 累积算法计算 display delay，等待后显示

#### Scenario: Frame rate read from input port format
- **WHEN** VideoSinkNode::Negotiate() 执行且上游已发布含 frame_rate 的 VideoFormat
- **THEN** 节点自身取得帧率用于显示时序计算，无需外部调用 SetVideoFps

#### Scenario: Own clock updated regardless of role
- **WHEN** VideoSinkNode 显示一帧 PTS=5.0 且当前为从钟
- **THEN** 仍更新自身时钟为 5.0（为将来的漂移统计与主时钟切换保留可用时基）

#### Scenario: 时基在呈现时刻推进
- **WHEN** 从钟模式下等待 display delay 后呈现一帧
- **THEN** 时钟在 sleep 结束之后、呈现的同一时刻被设置，SHALL NOT 在计算延迟之前就推进

#### Scenario: 重绘不推进时基
- **WHEN** 重绘当前帧
- **THEN** 播放位置不变，`MasterClock()->Get()` 不因重绘而改变

#### Scenario: Missing video format fails negotiation
- **WHEN** 输入端口未连接或格式不是视频
- **THEN** Negotiate() 记录 ERROR 并返回 false

### Requirement: AudioSinkNode plays audio frames through SDL
系统 SHALL 定义 `AudioSinkNode`（Sink 类型），使用 SDL 音频输出。

AudioSinkNode SHALL 提供：
- 单个输入端口：接收 MediaFrame（media_type=kAudio）
- SDL 音频设备驱动：内部 Pull 数据、resample 到 SDL 格式
- 时基：自持一个时钟，通过 `ProvideClock()` 以最高优先级参与主时钟仲裁
- `Negotiate()`：SHALL 从输入端口格式读取 sample_rate / channels（格式推理属协商期职责）
- `Prepare()`：SHALL 仅打开 SDL 音频设备，不再做格式推理
- ThreadingMode：Active

**时钟 SHALL 报告已呈现位置而非已提交位置**：设置时钟时 SHALL 扣除设备队列中尚未播放的时长。扣除量 SHALL 在把当前帧交给设备**之前**测量 —— 此时队列恰好覆盖 `[已听到位置, 当前帧 pts)`，二者之差即已听到位置，是精确等式而非估算。

**缓冲深度 SHALL 只有一个定义处**，同时服务于喂入限流与时钟补偿。调整缓冲深度 SHALL NOT 改变同步精度。

#### Scenario: Audio clock drives master clock
- **WHEN** AudioSinkNode 消费完 PTS=5.0 的帧且设备队列中尚有 0.1s 未播放
- **THEN** 主时钟报告约 4.9（用户正在听到的位置），而非 5.0

#### Scenario: 调整缓冲深度不影响同步
- **WHEN** 把设备队列目标深度从 0.1s 改为 0.3s
- **THEN** 主时钟报告的位置不变，音画同步不受影响（仅抗欠载能力变化）

#### Scenario: 队列为空时无补偿
- **WHEN** 刚启动或 seek 后清空缓冲，设备队列为空
- **THEN** 补偿量为 0，时钟直接等于帧 PTS，无跳变

#### Scenario: Audio params resolved during negotiation
- **WHEN** AudioSinkNode::Negotiate() 执行
- **THEN** sample_rate 与 channels 从输入端口格式解析完成，Prepare() 直接用其打开 SDL 设备

#### Scenario: Missing audio params fails negotiation
- **WHEN** 输入端口格式既非 AudioFormat 也不含可用的 codec_params
- **THEN** Negotiate() 记录 ERROR 并返回 false

### Requirement: VideoSinkNode 在暂停态支持步进与重绘
暂停状态下 VideoSinkNode SHALL 停止推进显示时序，但 SHALL 响应两类请求：

- **步进（step）**：收到 `kSeek` 后取一帧并立即显示，随后自动回到暂停等待。语义对齐 FFplay 的 `step_to_next_frame`，SHALL NOT 引入"预览帧"之类的自创概念。
- **重绘**：收到 `kResize` 并应用新尺寸后，SHALL 重新呈现已保存的当前帧，SHALL NOT 从输入端口取新数据。

恢复播放时 SHALL 清除未消费的步进请求。

#### Scenario: 暂停态 seek 步进一帧
- **WHEN** 暂停中收到 {kSeek, T}
- **THEN** 节点取一帧显示后重新进入暂停等待，SHALL NOT 连续消费后续帧

#### Scenario: 步进取到的必然是目标帧
- **WHEN** 暂停态 seek，输入链路中残留 pre-seek 的在途帧
- **THEN** 端口校验已将其丢弃，节点取到的第一帧即为 post-seek 数据

#### Scenario: 重绘不消费新数据
- **WHEN** 暂停中应用了新窗口尺寸
- **THEN** 节点重绘已保存的当前帧，输入链路的队列长度不变

#### Scenario: 无当前帧时重绘安全
- **WHEN** 尚未显示过任何帧就需要重绘
- **THEN** 节点不做任何渲染，不崩溃

## ADDED Requirements

### Requirement: VideoSinkNode 在渲染线程应用窗口尺寸变化`VideoSinkNode` SHALL 在收到 `kResize` 时仅暂存目标尺寸，并在**自身渲染线程**上应用到渲染器，随后重绘当前帧。

`OnCommand` 由控制线程调用，SHALL NOT 直接触碰渲染器 —— 渲染器由渲染线程独占。暂存 SHALL 保证宽高作为整体被读到，不会出现新宽配旧高的组合。

#### Scenario: 渲染器只被渲染线程修改
- **WHEN** UI 线程在播放过程中广播 {kResize, w, h}
- **THEN** 渲染器的尺寸状态仅由渲染线程写入，不存在跨线程的无同步读写

#### Scenario: 宽高整体生效
- **WHEN** 连续快速拖拽窗口产生多次 kResize
- **THEN** 渲染线程读到的始终是某一次完整的宽高组合，不会混用不同次的宽与高

#### Scenario: 暂停态尺寸变化立即生效
- **WHEN** 暂停时收到 {kResize, w, h}
- **THEN** 渲染线程应用新尺寸并重绘当前帧，画面立即重新适配
