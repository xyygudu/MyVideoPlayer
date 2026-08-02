## Why

Sink 节点与它们管理的外部设备之间，职责边界有四处含混，共同的症状是**一个动作被拆到多处、或多个动作被塞进一处**。

**① 音频时钟报告的是"我喂进去了什么"，不是"用户听到了什么"。** `AudioSinkNode` 在把帧交给 SDL **之前**就 `clock_->Set(mf.pts())`，而 `ShouldThrottle()` 允许 SDL 里积压 100ms。实测（48kHz 立体声 AAC）主时钟稳定超前 **0.091~0.121s，均值约 0.106s**，且该值只统计 SDL stream 内部，不含驱动缓冲，是下界。

真正的问题不是这 106ms 本身（实测抖动几乎为零，恒定偏移可被感知系统适应，主观验收未察觉不同步），而是**同步精度被缓冲策略绑架**：`ShouldThrottle` 里那个 `/10` 的职责是防音频欠载，却同时决定了主时钟偏移。将来为弱机器把缓冲调到 300ms，音画会突然差 300ms，而调参者完全不会想到自己改的是同步精度。缓冲策略与同步精度本该正交。

**② `VideoSinkNode::RenderFrame` 一个函数做三件事**：推进时基、保存当前帧、渲染输出。名字只承诺第三件。而 `RedrawCurrent()` 故意不推进时基（正确），于是两条渲染路径的时钟语义不同却看不出来 —— 谁把 redraw 接到 `RenderFrame` 上，时钟就会倒退。FFplay 是分开的两次调用（`update_video_pts` 后 `video_display`）。

**③ 窗口尺寸变化被拆成两条路径**：`NotifyWindowResized` 既在 UI 线程直接写 `video_renderer_.Resize()`，又经 graph 广播 `kRedraw`。同一个用户动作两条路，而且 `VideoRenderer::window_width_/height_` 被 UI 线程写、渲染线程读，是两个普通 `int` 的无同步跨线程读写 —— 标准数据竞争（UB）。

**④ `MediaGraph::Flush()` 单独调用不安全。** 上一个变更把世代递增放在 `Seek()` 里而非 `Flush()` 里，于是 `Flush()` 会清空队列但不递增世代 —— 之后所有在途旧数据都被判为"当代"，全部漏过端口校验。目前无外部调用者，但接口暴露着这个陷阱，且不变量的维持依赖调用方记得配对。

## What Changes

- `AudioSinkNode` 在设置时钟前扣除 SDL 中尚未播放的时长，使时钟报告"正在听到的位置"；`ShouldThrottle` 的 100ms 提为具名常量并改用同一个 `QueuedSeconds()` 计算，让"缓冲深度"只有一个来源。
- `VideoSinkNode::RenderFrame` 拆为 `PresentFrame()`（只保存当前帧并渲染），时钟推进上移到调用点显式执行，与 `RedrawCurrent()` 形成对称的一对。
- `CommandType::kRedraw` 替换为 `kResize`（携带宽高）。`MediaPlayer` 不再直接调 `VideoRenderer::Resize`，改为广播命令；`VideoSinkNode` 在**渲染线程**上应用新尺寸并重绘。竞态随之消失，无需引入原子成员。
- `MediaGraph::Flush()` 自行递增 seek 世代，成为独立可用的安全操作；`Seek()` 相应简化为 `Flush() + SendCommand + 重置时钟`。世代与清队列的配对约束收敛到一处强制执行。

## Capabilities

### Modified Capabilities

- `graph-sink-nodes`: AudioSinkNode 的时钟语义改为"已呈现位置"；VideoSinkNode 拆分呈现与时基推进，并在渲染线程应用尺寸变化。
- `av-sync`: 主时钟 SHALL 报告已呈现位置而非已提交位置。
- `graph-command-control`: `kRedraw` → `kResize`（带宽高）；`Command` 结构扩展。
- `media-graph-core`: `Flush()` 递增世代；`Seek()` 组合语义。
- `seek-consistency`: 世代递增的责任从 `Seek()` 移到 `Flush()`。
- `video-renderer`: 尺寸变更 SHALL 在渲染线程应用。

## Impact

- 代码：`src/media/nodes/{audio_sink_node,video_sink_node}.{h,cc}`、`src/media/graph/{graph_command.h,media_graph.h,media_graph.cc}`、`src/media/media_player.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 行为变化：主时钟（进而进度条与视频同步基准）整体后移约 106ms —— 这是修正而非回归。
- 风险：改动位于 A/V 同步基准，需人工验收音画同步；尺寸变更路径改变，需验收窗口缩放。
- 范围外：同步策略可插拔化（`frame_timer_` 算法硬编码在节点内）仍记于 `docs/improvements/`；Effect 参数经裸指针直达节点的问题属 spec 表述过严，另行处理。
