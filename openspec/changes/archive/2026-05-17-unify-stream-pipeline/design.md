## Context

当前管线架构：`Demuxer → PacketQueue → Decoder → FrameQueue<T> → Renderer`，其中 `StreamContext<T>` 是一个模板 struct，聚合了 `PacketQueue + Decoder + FrameQueue<T>`。

存在的架构问题：
1. StreamContext 是 struct，成员 public，PlayerImpl 大量穿透访问内部组件
2. 模板特化代码冗余（Audio/Video 几乎相同，每新增流类型需完整复制）
3. Decoder 是具体类，无法扩展到字幕等不同解码 API
4. Demuxer 暴露 `FormatContext()` 泄漏 FFmpeg 内部类型
5. 格式映射（MapPixelFormat/MapSampleFormat）放在管线层而非渲染层

约束：
- C++17，MSVC 2022
- 公共 API（VideoFrame/AudioFrame）的接口签名不能破坏
- 需保持当前的线程模型（Demux 线程、Decoder 线程、Audio SDL callback、Video render 线程）
- 改动需逐步可验证（每步编译通过+测试通过）

## Goals / Non-Goals

**Goals:**
- 消除 StreamContext 模板，使其成为类型无关的具体 class
- 引入 MediaFrame 作为管线内部统一帧表示
- 引入 IDecoder 接口使解码器可扩展
- 修复 StreamContext 封装不足（Facade 方法集补全）
- 收敛 Demuxer API（移除 FormatContext 暴露）
- 格式映射迁移到正确的职责层

**Non-Goals:**
- 不实现字幕流支持（只建立可扩展的架构基础）
- 不改变线程模型或同步策略
- 不改变 A/V sync 算法
- 不改变公共 API 接口签名（VideoFrame/AudioFrame 的公共方法不变）
- 不引入 Filter 框架或 Pin/Connection 模型

## Decisions

### Decision 1: MediaFrame 是具体值类型，不是虚基类

**选择**: MediaFrame 是 move-only 的具体类（`AVFramePtr` + `double pts` + `MediaType` 枚举），不使用继承或虚函数。

**替代方案**:
- (A) 虚基类 `IFrame` + `VideoFrame`/`AudioFrame` 继承 → 引入堆分配 + vtable 开销，帧是热路径对象（每秒 24-60+ 个），性能不可接受
- (B) `std::variant<VideoFrame, AudioFrame>` → 管线层仍需知道所有具体类型，新增类型需改 variant 定义
- (C) 具体类 + MediaType 标签 → zero-cost 区分，管线层完全不关心帧内容

**理由**: 管线内部只需 `pts()` + `IsValid()` + 类型标签。类型特定的数据访问推迟到渲染边界。这与 MPV（`mp_image`）、VLC（`picture_t`）的做法一致。

### Decision 2: IDecoder 回调直接输出 MediaFrame

**选择**: `FrameOutputCallback` 签名改为 `std::function<void(MediaFrame frame, int serial)>`。IDecoder 在解码时从 `AVStream::codecpar->codec_type` 获知 MediaType，注入到 MediaFrame。

**替代方案**:
- (A) 保持 `void(AVFrame*, double, int)` 回调 + StreamContext 做包装 → StreamContext 需要知道 MediaType 来构造 MediaFrame，破坏类型无关性
- (B) Decoder 输出 MediaFrame，StreamContext 只做搬运 → StreamContext 完全类型无关

**理由**: MediaType 的天然来源是 Decoder（它知道自己在解码什么流），由 Decoder 注入最自然。StreamContext::Start() 简化为纯粹的"连线"操作。

### Decision 3: StreamContext 通过 unique_ptr\<IDecoder\> 持有解码器

**选择**: StreamContext 构造时接收 `std::unique_ptr<IDecoder>`，由创建者（PlayerImpl）决定注入哪种解码器实现。

**替代方案**:
- (A) StreamContext 内部创建 Decoder → 需要知道流类型来选择实现，破坏单一职责
- (B) StreamContext 持有具体 Decoder → 无法扩展到字幕

**理由**: 依赖注入，遵循依赖倒置原则。未来新增 SubtitleDecoder 只需在 PlayerImpl 中注入不同实现。

### Decision 4: VideoFrame/AudioFrame 增加内部工厂方法

**选择**: 在 frame_impl.h 中新增 `VideoFrame MakeVideoFrame(const MediaFrame&)` 和 `AudioFrame MakeAudioFrame(const MediaFrame&)`，在此处执行格式映射。

**替代方案**:
- (A) 公共 API 中增加 `VideoFrame(MediaFrame&&)` 构造函数 → 泄漏内部类型到公共接口
- (B) 在 Renderer 内部直接操作 `MediaFrame::RawFrame()` → 绕过公共帧抽象，失去类型安全

**理由**: 工厂方法是内部友元函数（与 `GetInternalFrame` 模式一致），不暴露在公共 API 中。格式映射在此处执行，是渲染边界的自然位置。

### Decision 5: Demuxer 提供 typed stream 访问器

**选择**: 新增 `AVStream* AudioStream()` / `AVStream* VideoStream()`，移除 `FormatContext()`。

**替代方案**:
- (A) 完全隐藏 AVStream*（返回自定义 StreamInfo 结构） → 过度封装，当前只有内部模块使用 Demuxer，AVStream* 在 core/src 内部是可接受的
- (B) 保留 FormatContext() 但标记 deprecated → 半措施，不如一步到位

**理由**: AVStream* 仍是 FFmpeg 类型，但它是"单个流的句柄"而非"整个容器的万能入口"。在内部模块间传递 AVStream* 是合理的（Decoder::Open 就需要它），但不应暴露 FormatContext 让调用者自由导航。

### Decision 6: FrameQueue 保持模板但管线统一使用 FrameQueue\<MediaFrame\>

**选择**: `FrameQueue<T>` 作为通用线程安全队列模板保留，管线中实例化为 `FrameQueue<MediaFrame>`。

**替代方案**:
- (A) 将 FrameQueue 改为非模板（固定存 MediaFrame） → 失去通用性，如果其他地方需要不同队列类型则不便
- (B) 保持 `FrameQueue<VideoFrame>` / `FrameQueue<AudioFrame>` → 无法统一 StreamContext

**理由**: FrameQueue 本身是一个 general-purpose 的线程安全有界队列，保留模板合理。StreamContext 使用 `FrameQueue<MediaFrame>` 消除模板传染。

## Risks / Trade-offs

**[Risk] 渲染边界转换增加一次 AVFrame ref/copy**
→ Mitigation: `MakeVideoFrame(MediaFrame&)` 使用 `av_frame_ref`（引用计数 +1，非深拷贝），开销可忽略（ns 级）。或使用 move 语义转移 ownership。

**[Risk] AudioRenderer 目前直接访问 `GetInternalFrame(AudioFrame&)`，改为接收 MediaFrame 后需调整**
→ Mitigation: AudioRenderer 内部可直接使用 `MediaFrame::RawFrame()`（内部 API），或先转换为 AudioFrame 再使用 `GetInternalFrame`。后者保持现有渲染逻辑不变。

**[Risk] 大范围重构可能引入回归 bug**
→ Mitigation: 按分层顺序改动（底层先行）：MediaFrame → IDecoder → StreamContext → Demuxer → PlayerImpl → Renderer。每层独立编译验证。

**[Trade-off] MediaFrame 不携带类型化格式枚举（PixelFormat/SampleFormat）**
→ 格式信息保留在内部 `AVFrame::format` 中，映射推迟到渲染边界。管线内部无需格式信息，这是可接受的。

**[Trade-off] IDecoder 的虚函数调用开销**
→ 每次调用仅在 Open/Start/Stop/SetDropUntilPts 这些低频操作上，解码热路径（DecodeLoop）在实现类内部，无虚函数开销。
