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

#### Scenario: Missing video format fails negotiation
- **WHEN** 输入端口未连接或格式不是视频
- **THEN** Negotiate() 记录 ERROR 并返回 false

## ADDED Requirements

### Requirement: VideoSinkNode 在暂停态支持步进与重绘
暂停状态下 VideoSinkNode SHALL 停止推进显示时序，但 SHALL 响应两类请求：

- **步进（step）**：收到 `kSeek` 后取一帧并立即显示，随后自动回到暂停等待。语义对齐 FFplay 的 `step_to_next_frame`，SHALL NOT 引入"预览帧"之类的自创概念。
- **重绘（redraw）**：收到 `kRedraw` 后以当前窗口状态重新呈现已保存的当前帧，SHALL NOT 从输入端口取新数据。

恢复播放时 SHALL 清除未消费的步进请求。

#### Scenario: 暂停态 seek 步进一帧
- **WHEN** 暂停中收到 {kSeek, T}
- **THEN** 节点取一帧显示后重新进入暂停等待，SHALL NOT 连续消费后续帧

#### Scenario: 步进取到的必然是目标帧
- **WHEN** 暂停态 seek，输入链路中残留 pre-seek 的在途帧
- **THEN** 端口校验已将其丢弃，节点取到的第一帧即为 post-seek 数据

#### Scenario: 重绘不消费新数据
- **WHEN** 暂停中收到 {kRedraw}
- **THEN** 节点重绘已保存的当前帧，输入链路的队列长度不变

#### Scenario: 无当前帧时重绘安全
- **WHEN** 尚未显示过任何帧就收到 {kRedraw}
- **THEN** 节点不做任何渲染，不崩溃
