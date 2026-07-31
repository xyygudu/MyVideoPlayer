## Context

v1 转码图 `Demux → Decoder → Encoder → MuxNode` 对 matroska/mp4 输出失败：`EncoderNode` 未设置 `AV_CODEC_FLAG_GLOBAL_HEADER`，libx264 不写 extradata，`avformat_write_header` 以 `AVERROR_INVALIDDATA` 失败（`ff_isom_write_avcc` 静默返回）。修复后暴露第二个缺陷：音频编码器要求每次 `avcodec_send_frame` 恰好 `frame_size` 个采样（AAC=1024），而 AC3 5.1 解码帧重采样后为 1536。

生命周期约定（`node.h`）：`Negotiate` = 纯格式推理（不分配资源），`Prepare` = 物化资源（`avcodec_open2`、格式上下文、IO）。`MediaGraph` 保证先按拓扑序完成全部 `Negotiate` 再执行任何 `Prepare`。

## Goals / Non-Goals

**Goals:**
- 让 global-header 决策遵循"协商期定契约、Prepare 期落地"的生命周期划分。
- 修复两类格式（需全局头的 mkv/mp4 与需带内头的 mpegts/avi）都能正确转码。
- 修复音频 `frame_size` 约束，消除 `nb_samples > frame_size` 的 EINVAL 刷屏。

**Non-Goals:**
- 不实现完整的 GStreamer 式双向 caps 协商（列为独立后续项，见仓库记忆）。
- 不处理 DemuxNode 在 `Negotiate` 打开文件、EncoderNode 两段式公布格式等既有职责泄漏（另行跟踪）。

## Decisions

### D1：global-header 需求在协商期决定、Prepare 期消费
- `MuxNode::Negotiate()` 用 `av_guess_format` 解析容器，把 `AVFMT_GLOBALHEADER` 结论写入各输入端口；`EncoderNode::OpenCodec()` 在 `avcodec_open2` 前通过 `output_port_->Peer()->NeedsGlobalHeader()` 读回并设置/不设置该 flag。
- **备选 a（facade 注入，即 ffmpeg CLI 做法）**：由 `Transcoder::BuildGraph` 把 `mux->NeedsGlobalHeader()` 传进 `EncoderNode` 构造函数。否决：让 EncoderNode 构造参数混入非 `EncodeParams` 内容，且把契约决策放在生命周期之外。
- **备选 b（完整 caps 协商）**：把该需求建模为 caps 能力字段并在 `Connect` 求交。暂缓：改动面大，用户计划专门推进 caps。
- 采用本方案的理由：决策仍在协商期（Mux 写端口），读取在 Prepare 期（时序由图的"先全部 Negotiate 后 Prepare"保证）；Encoder 只依赖通用 `InputPort` 接口，依赖方向不变。

### D2：用 InputPort 需求通道作为最小上游回流机制
- 新增 `InputPort::SetNeedsGlobalHeader/NeedsGlobalHeader`，作为 caps 落地前的临时通道。
- 接受"该通道未来会被 caps 取代"的演进代价；保持接口小、语义单点。

### D3：音频用 `AVAudioFifo` 做 `frame_size` 缓冲
- 重采样结果先写 FIFO，凑满 `codec_ctx_->frame_size` 再读入整帧送编码器；EOF 用 `av_samples_set_silence` 静音补齐尾帧。PTS 按音频 time_base（1/sample_rate）逐帧累加。
- 选 `AVAudioFifo` 而非手写缓冲：原生支持 planar 布局，复用 FFmpeg 既有机制（符合"复用现有机制"原则）。

### D4：容器需求解析移到 `MuxNode::Negotiate()`
- 构造函数恢复为纯 slot 搭建；`EncoderNode` 构造函数恢复为单一 `EncodeParams`，恢复 spec"Configure 参数：EncodeParams"表述。
- 失败兜底：无法推断容器时记录 WARN 并视为 `needs_global_header=false`（带内头），避免阻塞转码。

## Risks / Trade-offs

- `EncoderNode::OpenCodec` 依赖 `output_port_->Peer()` 非空 → 若编码器输出未连接则按 in-band 处理（peer 判空）。合法图必然已连接，风险低。
- 该需求通道是 caps 的过渡替身，未来 caps 落地时需迁移 → 保持通道语义单一，迁移成本低。
- EncoderNode"协商期只公布初步格式、Prepare 后才公布真实格式"的两段式仍存在（FFmpeg 固有：extradata 仅在 open 后可得）→ 本变更不解决，仅以注释与 spec 显式承认。

## Migration Plan

- 无部署/回滚需求：行为变更随代码提交；spec delta 同步进主 spec 后归档。
