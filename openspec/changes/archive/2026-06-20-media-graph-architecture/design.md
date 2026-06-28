## Context

当前项目是一个基于 FFmpeg + SDL3 + Qt 的学习型视频播放器，采用 FFplay 风格的线性管线架构：`Demuxer → PacketQueue → Decoder → FrameQueue → Renderer`。PlayerImpl 作为上帝类直接编排所有组件的生命周期和同步。该架构无法在解码和渲染之间插入滤镜节点，也无法支持转码、采集等非播放场景。

业界参考：GStreamer（Element + Pad 图模型）、OBS Studio（Source-Filter-Output 模型）、FFmpeg libavfilter（AVFilterGraph 链式模型）。本项目需要介于 GStreamer 和 MPV 之间的复杂度：足够灵活支持多场景组合，但避免 GStreamer 的动态拓扑和繁重格式协商。

## Goals / Non-Goals

**Goals:**
- 构建可组合的节点图框架（MediaGraph），支持播放、滤镜链、转码三种场景
- 统一的节点抽象（INode），一致的声明周期（Configure → Negotiate → Prepare → Start → Stop）
- 线程模型混合：轻量滤镜零线程开销（Passive），重计算独立线程（Active）
- 复用现有底层资产：AVFramePtr/AVPacketPtr RAII 包装、SeqLock Clock、MediaFrame
- 渐进迁移：Phase 1-2 内完成核心框架并移除旧代码

**Non-Goals:**
- 运行时动态拓扑变更（图在构建时确定；SetFilter 通过 Stop→Rebuild→Start 实现，不算运行时变更）
- 网络推拉流（RTMP/SRT）—— 留待 Phase 5
- 摄像头/录屏采集节点 —— 留待 Phase 5
- 字幕处理 —— 不在本次范围
- 自定义 GPU 滤镜（CUDA/OpenCL）—— 通过 FFmpeg hwfilter 间接支持

## Decisions

### Decision 1: MediaBuffer 使用 `std::variant<AVPacketPtr, MediaFrame>` 而非 type-erasure

**选择**: `std::variant`  
**替代方案**: `std::any`（type-erasure）、void* + tag enum

**理由**: `variant` 提供编译期类型检查，访问时 `std::get` / `std::visit` 无需 RTTI 开销，且类型集合封闭可控（当前只有 Packet 和 Frame 两种，未来可能扩展 raw buffer）。`std::any` 引入堆分配和 type_info 查找；void* 不安全。

**约束**: variant 的类型集合在编译期固定，新增数据类型需修改定义。但考虑到 Packet/Frame 已覆盖 90%+ 的媒体数据场景，这是可接受的。

---

### Decision 2: Link 使用策略模板而非运行时多态

**选择**: `template<typename CapacityPolicy> class Link`  
**替代方案**: 基类 + 虚函数容量方法；单个类 + if-else 分支

**理由**: 当前 PacketQueue（ByteCapacity）和 FrameQueue（CountCapacity）逻辑 95% 相同，仅容量判定不同。策略模板在编译期消除分支，保持与手写两个类相同的性能。Link 实例化两种（`ByteLink` / `CountLink`），类型别名隐藏模板细节。

**CapacityPolicy 接口**:
```cpp
// 字节容量策略（用于包级 Link）
struct ByteCapacity {
    int64_t max_bytes;
    static int64_t Size(const MediaBuffer& buf) { /* 返回 payload 字节数 */ }
};

// 计数容量策略（用于帧级 Link）
struct CountCapacity {
    int max_count;
    static int64_t Size(const MediaBuffer& buf) { return 1; }
};
```

---

### Decision 3: Transform 节点默认 Passive，由上游线程同步调用 Process()

**选择**: Passive 默认模式（无独立线程，无输入队列）  
**替代方案**: 所有节点 Active（独立线程 + 队列）；全 Push 模式

**理由**:

| 模式 | 延迟 | 线程数 | 上下文切换 | 适用场景 |
|------|------|--------|-----------|----------|
| 全 Active | +队列延迟×N | N×(滤镜数+2) | 频繁 | 所有 |
| Passive 默认 | 仅队列延迟×active数 | 仅 Active 节点 | 最少 | 轻量滤镜 |
| 全 Push | 0 | 1 | 0 | 全同步场景（转码） |

Passive 模式下游滤镜在上游 Active 节点（如 DecoderNode）的 Push 路径中被同步调用 `Process(buf)`，数据不出当前线程，无需跨队列拷贝。典型滤镜（scale/crop/eq）耗时 1-3ms，不会阻塞上游线程。若某个滤镜耗时超过阈值（如 AI 超分辨率 30ms+），可通过标记该节点为 Active 将其升级为独立线程节点。

```cpp
// OutputPort::Push 中的路由逻辑
void OutputPort::Push(MediaBuffer buf) {
    INode* downstream = peer_->owner();
    if (downstream->Threading() == ThreadingMode::kPassive) {
        // Passive: 同步调用，无队列，回调将结果传递到下一个节点
        downstream->Process(std::move(buf), [this](MediaBuffer out) {
            // 继续沿 Passive 链传递，或入 Active 节点的 Link
            next_port_->Push(std::move(out));
        });
    } else {
        // Active: 异步入队
        link_->Push(std::move(buf));
    }
}
```

---

### Decision 4: 格式协商上游驱动（Source → Sink 方向）

**选择**: 单向协商，上游节点决定输出格式，下游节点声明接受范围  
**替代方案**: GStreamer 双向 caps intersection（源声明能力集 + 汇声明接收集 → 取交集）

**理由**: 双向 caps 协商对简单管线过度设计。本项目的典型场景中，格式变换由节点主动执行（如 DecoderNode 产出 AV_PIX_FMT_YUV420P，后续 ScaleFilter 主动转换为目标分辨率）。不需要动态协商不同编码器之间的格式交集。如果下游不接受上游格式，在 Prepare() 阶段报错，由用户决定是否插入转换节点。

格式能力使用区间描述（如 `pixel_formats: {YUV420P, NV12}`、`width: [64, 4096]`），在 Port.Connect() 时取交集验证。

---

### Decision 5: Clock 全局化，Sink 节点引用

**选择**: Clock 由 MediaGraph 持有，注入到需要同步的 Sink 节点  
**替代方案**: 每个 Sink 独立维护自己的 Clock（当前做法）

**理由**: 当前 audio_clock 和 video_clock 独立，MasterClock 通过枚举选择。在转码等非实时场景中 Clock 无意义（应禁用）。将 Clock 提升为 Graph 级别资源：播放场景注入 Clock，转码场景不注入（全速处理）。统一 Clock 也简化了 seek 后时钟重置逻辑。

**现有 SeqLock Clock 实现保留**，接口微调为 `IClock` 抽象以便未来替换（如外部网络时钟同步）。

---

### Decision 6: Seek 通过 Graph.Flush() 广播 + serial 验证

**选择**: MediaGraph::Flush() 按拓扑顺序调用每个节点的 Flush()，统一递增 serial  
**替代方案**: 每个节点独立处理 seek（当前 StreamContext 做法）

**理由**: Serial 机制已在本项目验证有效（用于检测 seek 后残留帧）。在 Graph 架构中，serial 从 Link 层面提升到 MediaBuffer 层面，随数据流动。Flush() 沿拓扑序清空所有 Link + 递增全局 serial + 各节点内部 buffersrc/buffersink 等状态。新帧携带新 serial，消费端验证 serial 一致性丢弃过期帧。

---

### Decision 7: 渐进迁移，Phase 2 完成后删除旧代码

**选择**: 新图框架作为独立模块开发，Phase 2 验证通过后删除 PlayerImpl/StreamContext/PacketQueue/FrameQueue/IDecoder  
**替代方案**: 一步到位全量重构

**理由**: 图框架是全新代码，与现有代码无交叉依赖。Phase 1 纯框架（可编译但无功能），Phase 2 用 Node 重新实现播放 → 对比验证行为一致 → 删除旧代码。降低风险，保持每个提交可编译可运行。

---

### Decision 8: Passive 节点 Process() 使用 OutputCallback 而非返回值

**选择**: `void Process(MediaBuffer input, OutputCallback emit)`  
**替代方案**: `MediaBuffer Process(MediaBuffer)` 返回单个 buffer；`std::vector<MediaBuffer> Process(MediaBuffer)` 返回多个

**理由**: 媒体处理中 1→N 映射很常见（av_buffersink_get_frame 循环可能产出多帧；framerate 滤镜可能吞帧）。返回 `vector` 有堆分配开销；返回单值无法表达 0 或多帧场景。回调模式零分配、支持任意输出数量：

```cpp
using OutputCallback = std::function<void(MediaBuffer)>;

// 典型 Passive 滤镜实现
void AVFilterNode::Process(MediaBuffer input, OutputCallback emit) {
    av_buffersrc_add_frame(buffersrc_, input.AsFrame().RawFrame());
    AVFrame* filt_frame = av_frame_alloc();
    while (av_buffersink_get_frame(buffersink_, filt_frame) >= 0) {
        emit(MediaBuffer{MediaFrame{filt_frame, pts, type}});
        av_frame_unref(filt_frame);
    }
    av_frame_free(&filt_frame);
}
```

OutputPort::Push 在 Passive 路径中将 emit 回调链接到下一个端口的 Push()，形成递归传递链。

---

### Decision 9: DemuxNode 合并文件打开和解复用为单一 Source 节点

**选择**: 单个 `DemuxNode`（Source 类型）负责 avformat_open_input + av_read_frame  
**替代方案**: 拆分为 FileSourceNode（打开文件）+ DemuxNode（读包路由）

**理由**: FFmpeg 的 `AVFormatContext` 在 open 和 read 之间是紧耦合的——同一个 context 既存储了 I/O 状态也驱动 read 循环。拆成两个节点需要在它们之间传递 `AVFormatContext*`，但 MediaBuffer 的 variant 类型只包含 `AVPacketPtr | MediaFrame`，无法承载 context 指针。强行扩展 variant 违背封闭类型集合的设计初衷。

合并为单一 Source 是 FFplay/MPV/VLC 的统一做法。DemuxNode 无输入端口，仅有动态数量的输出端口（每个流一个），职责清晰。

## Risks / Trade-offs

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Passive 滤镜阻塞上游线程导致解码延迟增加 | 高负载滤镜可能引起音视频不同步 | Process() 内嵌耗时检测（>5ms 输出 WARNING 日志），提示用户升级为 Active |
| 格式协商覆盖不全导致某些码流无法播放 | 特定格式组合的管线构建失败 | Prepare() 阶段检测格式不匹配并在 spdlog 输出详细格式信息；必要时自动插入 swscale/swresample 转换节点 |
| Seek 在复杂图拓扑中传播不完整 | 残留旧帧或死锁 | Flush() 按拓扑序逐节点执行，每个 Link 的 serial 机制二次验证；单元测试覆盖多分支拓扑的 seek 场景 |
| Serial 从 Link 移到 Buffer 层面增加拷贝开销 | 每个 Buffer 多携带 4 字节 | 开销可忽略（sizeof(int)），且 Buffer 本身已是 move-only，不涉及数据拷贝 |
| variant 类型集合局限 | 未来引入新数据类型需改定义 | 当前仅 Packet/Frame 两种，预留第 3 个 variant slot 为 RawBuffer 占位 |

## Migration Plan

1. **Phase 1（核心框架）**: 新增 `src/core/include/mvp/graph/` + `src/core/src/graph/`，不修改任何现有文件。CMakeLists.txt 新增源文件。编译通过 + 单元测试覆盖。
2. **Phase 2（重建播放）**: 新增 `src/core/src/nodes/`，实现播放所需节点（DemuxNode/DecoderNode/VideoSinkNode/AudioSinkNode）+ MediaPlayer API。此时新旧代码并存，通过编译选项切换。验证播放行为一致后，删除旧代码（player.cc, demuxer.h/.cc, decoder.h/.cc, i_decoder.h, stream_context.h/.cc, packet_queue.h/.cc, frame_queue.h/.cc）。
3. **Phase 3（滤镜链）**: 新增 AVFilterNode，无需修改现有节点。SetFilter 采用 Stop→Rebuild→Start 策略。
4. **Phase 4（转码）**: 新增 EncoderNode/MuxNode，新增 Transcoder 高层 API。
5. **无回滚需求**：纯学习项目，允许直接向前演进。

## Open Questions

- ~~**Q1**: Passive 节点的 `Process()` 签名应为值语义还是原地修改？~~ **已解决**：采用回调签名 `void Process(MediaBuffer input, OutputCallback emit)`，支持 0/1/N 输出场景，避免堆分配。
- ~~**Q2**: AVFilterNode 是否应支持多输入？~~ **延后**：Phase 3 初版单入单出，overlay 场景留待实现时再设计多 InputPort 索引。
- ~~**Q3**: MediaGraph 是否需要支持子图？~~ **已解决**：初版扁平化 DAG，不引入层级。
- **Q4**: SetFilter 重建图时如何最小化中断时间？初版简单 Stop→Rebuild→Start，后续可优化为主线程预构建新图后原子切换。
