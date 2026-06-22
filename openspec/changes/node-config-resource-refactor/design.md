## Context

MediaGraph 节点图框架（Phase 1-2）已完成播放功能。但 BuildGraph 中存在三处架构缺陷：NodeConfig 上帝结构体、双重 avformat_open_input、HW 设备归属不当。参照 GStreamer 的三层模型（element 属性 / pad caps / pipeline context），需将三类数据按本质分流。

当前代码状态：
- `NodeConfig` 包含 8 个字段但每个节点只用 0-1 个，`Configure()` 大多为空壳
- `DecoderNode::SetStream(AVStream*)` 需要外部传入 FFmpeg 内部指针，耦合严重
- MediaPlayer 二次打开文件（avformat_open_input）仅为拿 AVStream* 喂解码器
- `INode::Negotiate()` 是空壳 stub，本应承担格式协商职责

## Goals / Non-Goals

**Goals:**
- 消除 NodeConfig 上帝结构体，配置回归节点专属强类型
- 实现真正的端口格式协商：流参数通过 MediaFormat 顺着连接自动流动
- HW 设备提升为 Graph 级共享资源，支持未来多节点共享（硬解+硬编零拷贝）
- MediaPlayer::BuildGraph 大幅简化（删除二次 open、删除手动 SetStream/SetHWAccel）
- 三层模型覆盖全部未来场景（转码/录屏/摄像头/推拉流），不再需要破坏性重构

**Non-Goals:**
- 不改变播放行为或音视频同步逻辑
- 不引入运行时动态格式重协商（初版只在 Negotiate 阶段静态协商一次）
- 不重构 HWAccelContext 内部实现（仅改变归属）

## Decisions

### Decision 1: MediaFormat 存储 `shared_ptr<AVCodecParameters>`（深拷贝 + 自定义 deleter）

**选择**：每个 MediaFormat 持有 codecpar 的共享引用计数拷贝。
**替代 A**：存 `AVStream*` 引用 → 否决。生命周期不安全 + 未来非文件源（摄像头/录屏）根本没有 AVStream。
**替代 B**：每次拷贝 MediaFormat 都 deep copy codecpar → 否决。浪费（端口 get/set 间多次拷贝）。

**理由**：
1. 生命周期安全：shared_ptr 引用计数管理，不依赖 DemuxNode 的 format_ctx_ 存活
2. 拷贝成本可忽略：整个生命周期只在建图时拷贝一次 codecpar（几百字节），非每帧
3. 决定性：摄像头/录屏节点无 AVStream，只有拷贝模式能让所有源类型统一参与协商
4. shared_ptr 让 MediaFormat 保持廉价可拷贝，无需改 move-only 牵连端口代码

### Decision 2: 从 INode 接口移除 Configure(NodeConfig)，配置改为构造参数

**选择**：节点配置通过构造参数或节点专属 struct 注入，INode 接口只保留生命周期方法。
**替代**：NodeConfig 拆为基类 + 派生（dynamic_cast 下行转换）→ 否决，类型不安全。

**理由**：配置是节点专属的。建图者（MediaPlayer/Transcoder）创建具体类型时就知道它需要什么参数。只有生命周期 + 数据流需要多态，这是 GStreamer/DirectShow 的共识。

### Decision 3: HW 设备提升为 MediaGraph 共享资源

**选择**：`MediaGraph::SetHWDevice(shared_ptr<HWAccelContext>)`，与 Clock 平行设计。需要 HW 的节点从 graph 查询。
**替代**：HW 设备搬进 DecoderNode 内部 → 否决。未来转码「硬解+硬编共享设备零拷贝」无法实现。

**理由**：HW 设备是管线级共享资源（GStreamer 的 GstContext / FFmpeg 的 AVHWDeviceContext 均为 pipeline 级）。与 Clock 的 graph 全局设计对称。

### Decision 4: OutputPort::Connect 时传播格式到下游 InputPort

**选择**：Connect() 内部调用 `peer->SetFormat(format_)` 将上游格式同步到下游。
**替代**：在 Negotiate 阶段由 MediaGraph 遍历连接手动传播 → 否决，额外复杂度。

**理由**：Connect 在建图期调用（早于 Negotiate），此时 DemuxNode 已 Prepare 完毕、输出端口格式已就绪。下游 Negotiate 时直接从 InputPort::Format() 读取即可，无需额外传播步骤。

### Decision 5: DecoderNode 从 graph 查询 HW 设备（SetGraph 模式）

**选择**：DecoderNode 持有 `MediaGraph*`（非拥有），Prepare 中通过 `graph_->HWDevice()` 获取。
**替代**：通过 Negotiate 参数传递 → 否决，HW 设备不是流格式的一部分。

**理由**：HW 设备是 graph 级共享资源，不同于流元数据。用与 VideoSinkNode/AudioSinkNode 已有的 SetGraph 模式一致，保持对称。

## Risks / Trade-offs

| 风险 | 影响 | 缓解 |
|------|------|------|
| Negotiate 顺序依赖：下游 Negotiate 时上游格式必须就绪 | DecoderNode 拿不到 codecpar | Connect 在建图期即传播格式（早于 Negotiate）；MediaGraph::Negotiate 按拓扑序执行 |
| AudioSinkNode 原从 AVStream 取采样率/声道数 | 配置失败 | 从 codec_params 读取（AVCodecParameters 含 sample_rate/ch_layout），或 MediaFormat 补 audio 字段 |
| NodeState::kConfigured 语义变化 | 状态机不一致 | Prepare 同时接受 Idle/kConfigured 入口，最小改动 |
| 过渡期双轨代码 | 暂时复杂度增加 | 步骤 7 统一清理过渡代码，保证每步可编译 |

## Migration Plan

采用 8 步渐进实施（每步可编译，详见 tasks.md）：
1. MediaFormat 纯新增扩展（不破坏现有）
2. Port 格式传播（补一行）
3. DemuxNode 输出端口携带完整参数
4. DecoderNode 协商重构（双轨过渡）
5. HW 设备提升 graph（双轨过渡）
6. MediaPlayer 简化（切换到协商路径）
7. 移除过渡代码 + NodeConfig（最终清理）
8. 全量构建 + 运行验证
