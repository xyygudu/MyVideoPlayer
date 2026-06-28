## Context

MediaGraph 节点图（Phase 1-2）已完成播放。但代码审查发现四个架构缺陷 + 10 个超 50 行函数 + 1 处 goto。四缺陷指向同一病根：MediaGraph 抽象太弱，编排逻辑泄漏到 MediaPlayer/BuildGraph。参照 GStreamer 三层模型（element 属性 / pad caps / pipeline context）统一重构。

当前问题代码：
- `MediaPlayer::Impl` 持有 5 个单节点指针，Seek/Pause 里逐个调用节点专属方法
- `DecoderNode::Negotiate` 只缓存 codecpar，无真正格式工作；输出格式却在 Prepare 设置
- `MediaFormat` 扁平结构体混杂 video（width/height/pixfmt）+ audio（sample_rate/channels）+ packet（codec_params）字段
- `BuildGraph` 104 行手写整条管线；`DecodeLoop` 75 行含 goto

## Goals / Non-Goals

**Goals:**
- MediaPlayer 与拓扑解耦：通过 graph 高层操作（Seek/SetPaused）控制，不持有单个节点
- MediaFormat 用 variant 拆分，消除胖结构体，codec_params 归位到 EncodedFormat
- Negotiate（纯格式推理）/ Prepare（资源分配）职责厘清
- 源探测形式化为独立阶段，DemuxNode 不再特殊
- BuildGraph 用 Builder 封装，滤镜就绪
- 消除所有超 50 行函数和 goto
- 主流程先跑通无 bug，StepFrame 等细节功能留扩展点

**Non-Goals:**
- 不实现 StepFrame（暂停步进），仅预留 Command 扩展点
- 不实现滤镜链 / 转码（仅让 Builder 滤镜就绪）
- 不改变播放行为或音视频同步算法
- 不引入运行时动态格式重协商

## Decisions

### Decision 1: MediaFormat 用 variant，codec_params 归入 EncodedFormat 分支

**选择**：`std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat>`，codec_params 只在 EncodedFormat。
**替代 A**：codec_params 做 MediaFormat 公共字段 → 否决，裸帧格式也会带永远为 null 的 codec_params 槽（胖结构体毛病）。
**替代 B**：基类+子类多态 → 否决，闭集类型误用 OCP，破坏值语义（MediaFormat 经端口值拷贝），引入堆分配+虚函数。

**理由**：存在两种本质不同的格式——编码格式（codec_params + extradata，Demux→Decoder）和裸帧格式（width/height 或 sample_rate，Decoder→Sink）。FFmpeg 自己 AVCodecParameters 与 AVFrame format 字段就是分开的。codec_params 归 EncodedFormat 分支，裸格式不带它。media_type/time_base 作为公共字段（路由需要）。

调用点强制 `fmt.AsVideo().width`，不保留 `fmt.width()` 转发——转发会把胖接口请回来、隐藏类型、泄漏抽象。改动量大但架构正确。

```cpp
struct EncodedFormat { int codec_id; std::shared_ptr<AVCodecParameters> codec_params; };
struct VideoFormat   { int width, height; PixelFormat pixel_format; Rational frame_rate; };
struct AudioFormat   { int sample_rate, channels; SampleFormat sample_format; };
class MediaFormat {
    MediaType media_type_;
    Rational time_base_;
    std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat> payload_;
};
```

### Decision 2: 事件化控制，MediaPlayer 不持有单个节点

**选择**：Command 机制 + MediaGraph 高层操作。MediaPlayer 只持有 graph。
**替代**：保留单节点指针逐个调用 → 否决，门面被拓扑绑死，加滤镜节点就得改 MediaPlayer。

**理由**：GStreamer 模型——应用层只持有 pipeline，控制以事件流过图。当前 StepFrame 不做，但保留 Command 机制（OCP）：加 StepFrame 只需加枚举值，不改接口。

```cpp
enum class CommandType { kSeek };   // 当前只此一个
struct Command { CommandType type; double position{0.0}; };

class INode { virtual void OnCommand(const Command&) {} };

void MediaGraph::Seek(double pos) {
    Flush();                                  // graph 操作：清 Link 队列 + serial++
    SendCommand({CommandType::kSeek, pos});   // 节点各自响应（机制下沉）
}
void MediaGraph::SetPaused(bool p) {
    for (auto* n : topo_order_) n->SetPaused(p);  // 状态级联
}
```

seek 的机制下沉到节点：DemuxNode 重定位、DecoderNode 设 drop、AudioSinkNode 清 SDL。MediaPlayer::Seek 收敛为 `graph_->Seek(t)` + clock reset。

### Decision 3: Negotiate（格式推理）/ Prepare（资源分配）职责厘清

**选择**：Negotiate 从 codecpar 算出输出格式（不开 codec），Prepare 只开 codec。
**理由**：AVCodecParameters 自带 width/height/sample_rate，无需开 codec 即可推理输出格式。分离"决定格式"（纯、可 fail-fast）与"为格式分配资源"（重），整图格式兼容性可在分配任何资源前校验，为滤镜链插转换节点打基础。

### Decision 4: 源探测形式化为独立阶段

**选择**：`ISourceNode::Probe()` 返回 `vector<StreamInfo>`（含 duration），MediaPlayer 用它建拓扑，之后图统一 Negotiate→Prepare。
**理由**：拓扑依赖源内容（几条流、什么编码）是固有约束（GStreamer 动态 pad 也承认两阶段）。把它从隐式 hack 提升为显式一等公民阶段，DemuxNode 不再在 BuildGraph 里享受特殊待遇。Prepare 幂等（format_ctx_ 判空守卫）。duration 随 Probe 返回，缓存到 MediaPlayer，CurrentPosition 读 clock——MediaPlayer 彻底不持有节点。

### Decision 5: PlaybackGraphBuilder 滤镜就绪的链式设计

**选择**：`AddVideoPipeline(stream, filters={})` 构建线性链，滤镜作为列表（现空将来填）。Builder 用 Context 结构体注入依赖（非长参数列表）。
**替代**：`AddVideoBranch(stream)` 写死 Decoder→Sink → 否决，不适合插滤镜。

**理由**：滤镜链本质是 Decoder 和 Sink 之间插更多 Transform 节点。把分支建成可组合的链，滤镜从第一天就是一等公民（传空列表）。共享底层 `ConnectChain` 链连接工具，未来 TranscodeGraphBuilder 复用。Context 结构体避免同类型指针传错位（两个 Clock*）。

```cpp
struct PlaybackContext { MediaGraph* graph; VideoRenderer* renderer;
                         Clock* audio_clock; Clock* video_clock;
                         HWAccelContext* hw_device; void* window_handle;
                         VideoFrameCallback video_cb; };
class PlaybackGraphBuilder {
    explicit PlaybackGraphBuilder(const PlaybackContext& ctx);
    void AddVideoPipeline(const StreamInfo& s, const std::vector<FilterSpec>& filters = {});
    void AddAudioPipeline(const StreamInfo& s, const std::vector<FilterSpec>& filters = {});
};
```

### Decision 6: 消除 goto + 长函数提炼

DecodeLoop 的 goto 根因是 EOS 后复用 process_packet。提炼 `MaybeFlushOnSerialChange`/`ProcessPacket`/`HandleEos` 后自然消除——EOS 分支 drain 后 continue，新数据走正常 Pull 路径。其余 9 个长函数按职责提炼私有辅助方法。

## Risks / Trade-offs

| 风险 | 影响 | 缓解 |
|------|------|------|
| variant 改动牵连大量 `fmt.width()` 调用点 | 编译错误面广 | 步骤 1 优先做，grep 全部调用点逐一迁移到 AsVideo/AsAudio |
| DecodeLoop 消 goto 后 EOS+新数据时序 | seek 后首帧处理 | EOS drain 后 continue，新数据正常 Pull，单元验证 seek |
| Probe + Prepare 幂等 | 重复打开文件 | format_ctx_ 判空守卫 |
| Command 当前只 kSeek 可能显冗余 | 过度设计质疑 | 一个 enum+struct+virtual 成本极低，换来 OCP 扩展性 |
| MediaGraph::Seek 与节点 OnCommand 时序 | seek 命令到达顺序 | SendCommand 按拓扑序；Flush 先于 SendCommand |

## Migration Plan

7 步渐进（每步可编译可运行）：
1. MediaFormat variant 重构 + Intersect 泛型化 + 迁移所有调用点
2. Negotiate/Prepare 职责厘清（decoder 输出格式挪到 Negotiate）
3. 长函数提炼 + 消 goto（纯重构）
4. 源探测形式化（ISourceNode::Probe，Prepare 幂等）
5. PlaybackGraphBuilder（BuildGraph 瘦身）
6. 事件化控制（Command/OnCommand/SendCommand，MediaPlayer 删单节点成员）
7. 全量构建 + 运行验证（播放/seek/暂停/同步/HW）

## Open Questions

无（三个待确认问题已定：Command 广播+节点过滤；Builder 用 Context 结构体；调用点强制 AsVideo 不保留转发）。
