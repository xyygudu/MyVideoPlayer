## Context

管线是一张 MediaGraph：每个 Active 节点自带线程，节点之间是有界 Link。播放侧 facade 是 `MediaPlayer`，转码侧是 `Transcoder`。此前节点通过 `MediaGraph::SetEventCallback` 在自己的线程里直接回调 facade，facade 在回调中又去停/改同一张图，形成跨线程重入；状态 `GraphState` 名义上由图管理，实际由 facade 修改。

## Goals / Non-Goals

**Goals:**
- 节点上报与应用层状态解耦，状态只有一个所有者、一个写线程模型
- 暂停时特效修改所见即所得；特效可重置
- 可在运行期切换解码/特效/呈现路径并观察管线实际状态，便于对照调试
- 让修复可被自动化验证，回归可被发现

**Non-Goals:**
- 不做 AVFilter 节点、不做 10-bit 特效、不改主时钟仲裁
- 播放不折叠时间戳跳变（与 ffplay 一致）

## Decisions

### D1 消息总线代替事件回调
节点线程 `Post(GraphMessage)`（不阻塞），facade 的单一事件线程 `Wait()` 后在 facade 锁内处理。对照：GStreamer `GstBus`、mpv 的事件队列都是"投递—消费"模型；ffplay 是单进程循环直接读标志位。选择总线的原因是 facade 在处理 EOS/错误时需要停图，而停图要 join 投递者线程——回调模型下这是自我 join。
- 消息携带 seek 世代 `serial`，事件线程丢弃旧世代消息，EOS 与 seek 的竞态由世代比较消解。
- 完成回调在事件线程、锁外调用；回调里 `Open/Close` 被拒绝（会 join 自身）。
- 设备丢失重建由事件线程自己替换图；`EventLoop` 每轮重新读取 `graph_->Bus()`。

### D2 暂停刷新 = 对屏幕帧做一次 seek（mpv refresh seek）
特效在 sink 上游，已显示的帧不会再经过特效链。备选：
- (a) sink 缓存原始帧并在渲染线程重跑特效 —— 特效节点是 Passive、在解码线程同步执行，渲染线程重跑会引入第二条执行路径和锁；
- (b) mpv 的 refresh seek：seek 到屏幕帧的精确 pts，解码器丢弃 pts 严格小于目标的帧，同一帧经新特效链重新显示。

选 (b)，复用现有 seek 世代机制，不新增路径。合并策略：一次只允许一个刷新在途，`settled_epoch_` 记录"最新世代已上屏（`kFirstFrame`）或已到 EOS"，期间的修改只置 `refresh_again_`，上屏后补做一次——拖动滑块不会触发连串硬解重建。用户 seek 会清掉待补刷新（新 seek 本身就经过新参数）。

### D3 选项变更 = 在当前位置重建管线
`SetOptions` 停事件线程 → 记录屏幕帧位置、播放状态、特效设置 → 拆图 → 按新选项建图 → 恢复特效 → seek 到原位置 → 恢复播放/暂停。与设备丢失恢复共用 `RebuildLocked`。对照：mpv 切 `hwdec` 时重建解码器（不重建整条链），VLC 重建 vout；本项目节点能力在协商期确定，局部重建会破坏"协商期定型"的约束，因此整图重建。

### D4 低帧率同步沿用 ffplay compute_target_delay
`diff = 视频时钟（屏幕帧外推）− 主时钟`，而不是"下一帧 pts − 主时钟"；`|diff| ≥ kMaxFrameDuration(10s)` 视为时间戳跳变不修正；视频超前且帧长 > `kSyncThresholdMax` 时一次性 `delay += diff`。等待按 `kMaxSleepSeconds` 分片轮询暂停/seek/停止，但总时长不再封顶——原实现封顶 0.1 秒导致 <10fps 内容跑快。

### D5 时间戳：播放与转码语义分开
DemuxNode 统一按容器 `start_time` 平移到 0（mpv/ffplay 的显示时间轴都从 0 开始）。TS 跳变折叠只在转码打开（`DemuxOptions::correct_ts_discontinuities`），实现对照 ffmpeg 命令行的 `ts_discontinuity_detect`：跨流共享一个偏移量，DTS 后跳或前跳超过阈值即累加偏移。播放不折叠，与 ffplay 一致（播放器按 serial/时钟重置处理跳变即可）。

### D6 编码端时间戳与格式
- 视频 pts 不严格递增的帧直接丢弃（ffmpeg `-fps_mode vfr` 的行为），避免 x264 产生非法 DTS、muxer 拒包。
- 编码参数经 `avcodec_get_supported_config` 协商；源尺寸/像素格式变化时缩放到编码器尺寸，音频经共享的 `AudioResampler` 跟随输入变化。

### D7 变换几何以像素中心为坐标
整数坐标是像素中心，n 个像素的中心在 (n−1)/2；原实现用 n/2，镜像变成 x→n−x：GPU 路径首列采样越界成黑边且整体错位一像素（色度错位两像素）。CPU 置换路径另有两处缺陷：任意角度被量化成 0/90/180/270 后走置换（10° 旋转在 CPU 上无效），NV12 置换只拷 U 分量。修正后三条路径（GPU、下载后的 NV12 CPU、软解 yuv420p CPU）输出一致，由测试逐路径比对。

### D8 测试基础设施
- 核心源码编成 `mvp_core_objects`（OBJECT 库），`mvp_core.dll` 与 `mvp_tests` 共用同一份目标文件；测试定义 `MVP_CORE_STATIC_DEFINE`，可以直接访问内部类。
- 素材由 FFmpeg 自带的 `ffmpeg.exe` 在构建期用 lavfi 生成（确定、体积小、不入库）；真实素材测试通过 `MVP_USER_MEDIA_DIR` 显式启用。
- 播放集成测试渲染到隐藏的 Win32 窗口，音频用 SDL dummy 驱动（实时节奏、无声），通过公共 API（状态、位置、`CaptureFrame`）观察结果。

## Risks / Trade-offs

- [refresh seek 需要重新解码 GOP] → 合并在途刷新；长 GOP 4K 素材下暂停调参仍有数百毫秒延迟，可接受
- [整图重建会打断音频] → 只在用户切换调试选项或设备丢失时发生
- [测试依赖 GPU 与音频驱动] → 音频用 dummy 驱动；无 D3D11 硬解的机器上 GPU 路径测试退化为软件路径，仍然通过
- [配置期下载 GoogleTest] → URL 固定版本并校验 SHA256；可用 `-DMVP_BUILD_TESTS=OFF` 关闭
