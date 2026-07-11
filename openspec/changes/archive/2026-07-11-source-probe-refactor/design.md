## Context

当前 `DemuxNode` 在构造函数中通过 `InitStreamInfo()` → `OpenFile()` 提前打开文件并探测流信息，外部通过 `StreamInfoMap()` 获取。这导致 `BuildGraph` 必须"先构造 DemuxNode → 查流信息 → 创建 Decoder/Sink → 再 Prepare"，节点创建和配置被迫分散在两个阶段。

`Prepare()` 中的 `OpenFile()` 设置了 `if (format_ctx_) return true;` 的幂等守卫，使得构造函数打开的文件句柄在 Prepare 阶段被复用——这实际已是"两阶段打开"的雏形。

## Goals / Non-Goals

**Goals:**
- 将探帧职责从 `DemuxNode` 构造函数中剥离到独立的 `SourceProbe`
- 设计通用的 `SourceInfo` 结构体，可复用于 UI 展示、流切换、CanPlay 等场景
- `BuildGraph` 职责简化为：探帧 → 建图 → 加节点（含配置）→ 连线 → 完成
- `DemuxNode` 构造函数不再打开文件，`Prepare()` 成为唯一打开路径

**Non-Goals:**
- 不引入 `PlaybackGraphBuilder` 类或 `PlaybackContext` 结构体（过度封装）
- 不封装连线函数（留 inline 以支持未来滤镜链扩展）
- 不修改 SinkNode 的配置方式（保持 Setter）
- 不实现流切换功能（仅保证 SourceInfo 结构体能支持未来扩展）

## Decisions

### Decision 1: SourceProbe 作为独立工具类，而非 ISourceNode 接口

**选择**：`SourceProbe::Probe(filepath) → SourceInfo` 作为静态工具方法。

**备选**：ISourceNode::Probe() 虚函数接口，由 DemuxNode 实现。

**理由**：
- 探帧不需要多态——总是对一个文件路径做 `avformat_open_input` + `avformat_find_stream_info`
- 独立工具类可以在不创建任何 Graph Node 的情况下调用（如 `CanPlay(filepath)`）
- 避免 DemuxNode 同时持有"探帧者"和"读包者"两重身份
- MPV 采用相同模式（`mp_probe` 独立于 `demux_open`）

### Decision 2: SourceInfo 包含 VideoStream / AudioStream 子结构

**选择**：

```
SourceInfo
├── filepath, duration, format_name, bit_rate   (文件级)
├── video_streams: vector<VideoStream>
│   └── index, codec_name, width, height, frame_rate, pix_fmt, bit_rate
└── audio_streams: vector<AudioStream>
    └── index, codec_name, sample_rate, channels, channel_layout, sample_fmt, bit_rate
```

**备选**：复用现有 `StreamInfo`（`node.h`）+ `MediaFormat`（`media_format.h`）。

**理由**：
- 现有 `StreamInfo` 内嵌 `MediaFormat`（含 `std::shared_ptr<AVCodecParameters>`），强绑定 FFmpeg 类型和引用计数语义，不适合作为公共数据结构
- `SourceInfo` 中 `codec_name` 用 `string` 而非 `AVCodecID` 枚举，对 UI 友好且解耦 FFmpeg 头文件
- 分离 `video_streams[]` 和 `audio_streams[]` 比单一的 `streams_` map 更直观，调用方不需要按 `MediaType` 过滤
- 参考了 FFmpeg 的 `AVFormatContext` / `AVStream` 层级，是业界通用的信息模型

### Decision 3: DemuxNode 不再在构造时打开文件

**选择**：`DemuxNode(filepath)` 仅保存路径和根据 `SourceInfo` 预知的信息创建端口。文件打开推迟到 `Prepare()`。

**当前行为**：
```
DemuxNode(filepath)
  └── InitStreamInfo() → OpenFile() → avformat_open_input  ← 构造时打开
       ↓
  Prepare() → OpenFile() → if (format_ctx_) return true;   ← 幂等跳过
```

**目标行为**：
```
SourceProbe::Probe(filepath) → SourceInfo  ← 独立探帧，用完即关
BuildGraph:
  DemuxNode(filepath, video_idx, audio_idx) ← 构造函数不打开文件
  ...
  Prepare() → OpenFile() → avformat_open_input  ← 唯一打开路径
```

**理由**：
- 构造时打开文件是"副作用式"设计，调用方难以控制生命周期
- 探帧已经在 `SourceProbe` 中完成，DemuxNode 不需要重复探测
- `Prepare()` 成为唯一打开路径，符合 Graph 生命周期语义（Prepare = 初始化资源）

### Decision 4: BuildGraph 保持函数形式，不引入 Builder 类

**选择**：BuildGraph 保持为 `MediaPlayer::Impl` 的私有方法，通过预探帧消除数据依赖后自然缩短到 ~35 行。

**备选**：引入 `PlaybackGraphBuilder` 类（`playback-graph-builder` spec 中的方案）。

**理由**：
- BuildGraph 的复杂度根源是数据依赖而非逻辑分支——预探帧解决了根因，不需要 Builder 模式
- Builder 类需要 `PlaybackContext` 结构体来注入 6+ 个依赖（renderer、clocks、callbacks...），本质上只是把参数打包，没有减少复杂度
- 未来加入滤镜链时，BuildGraph 保持 inline Connect 比通过 Builder 的 `AddPipeline` 方法更灵活（加滤镜只需多一行 Connect）

### Decision 5: 连线不封装

**选择**：`graph_->Connect(...)` 调用直接写在 BuildGraph 中，不提取为独立函数。

**理由**：
- 4 条 Connect 调用本身已经是 4 行，封装函数反而增加跳转成本
- 未来加滤镜链时只需在 Decoder → Sink 之间插入 `graph_->Connect(decoder_out, filter_in, ...)` + `graph_->Connect(filter_out, sink_in, ...)`，不改变任何函数签名

## Risks / Trade-offs

- **[风险] 文件被打开两次**：`SourceProbe::Probe` 和 `DemuxNode::Prepare` 各调用一次 `avformat_open_input`。→ **缓解**：第二次打开时文件头已在 OS 页缓存中，实际开销 <1ms；MPV 采用相同策略，已验证可行。

- **[风险] StreamInfo 共享指针生命周期**：当前 `StreamInfo::format` 内嵌 `std::shared_ptr<AVCodecParameters>`，`MediaPlayer::streams_` 和 `DemuxNode` 共享同一份 codec 参数。重构后 `SourceInfo` 不复用此结构，codec 参数在 DemuxNode 内部私有不外泄。→ **缓解**：`SourceInfo` 中 `codec_name` 是普通 string，无生命周期问题。

- **[权衡] ISourceNode 接口被废弃**：`source-probe` spec 中定义的 `ISourceNode::Probe()` 虚接口不再需要。→ **决策**：该 spec 尚未被任何代码实现，废弃无实际影响。

## Migration Plan

1. 新增 `SourceInfo` 头文件和 `SourceProbe` 类 → 不影响现有代码
2. 修改 `BuildGraph`：在最前面调用 `SourceProbe::Probe()`，用 `SourceInfo` 替代 `demux->StreamInfoMap()`
3. 修改 `DemuxNode`：构造函数移除 `InitStreamInfo()` 调用，改为接收流索引参数
4. 移除 `DemuxNode::StreamInfoMap()`（确认无其他调用方后）
5. 编译验证 + 功能回归（播放、暂停、Seek、EOF 均不受影响）
6. 更新 `source-probe` 和 `playback-graph-builder` spec 的 delta

## Open Questions

无。所有技术决策已在讨论中达成一致。
