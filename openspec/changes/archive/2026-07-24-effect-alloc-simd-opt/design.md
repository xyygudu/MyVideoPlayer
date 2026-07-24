## Context

现状分析见 `docs/interview` 之前的 explore 讨论（未落盘），核心结论：

1. `MediaFrame::MakeWritable()`（`media_frame.cc`）实现上先 `av_frame_ref(tmp, frame_.get())` 把底层 buffer 引用计数顶到 2，再调用 `av_frame_make_writable(tmp.get())`——判断永远为"不可写"，因此**总是**深拷贝整帧，即便调用方（`ColorEffectNode::Process`）传入的 `input` 在到达这里时其实是唯一持有者（`DecoderNode`/`Port::Push`/`MediaBuffer` 全程只有 move，没有额外 ref）。
2. `TransformEffectNode::Process()` 每帧调用 `MediaFrame::CreateSameFormat` → `av_frame_get_buffer(mf.frame_.get(), 0)`，即每帧一次全新分配。4K NV12 单帧 ~12MB，命中系统级大内存分配路径（Windows 上大概率是 `VirtualAlloc`），分配耗时不稳定，表现为播放卡顿（帧间抖动）而非稳定慢。
3. `pixel_ops` 的 `ApplyLut`（Color）和 `RemapPlane`/`RemapInterleavedPlane`（Transform 双线性路径）仍是逐像素标量实现，是非快速路径场景（非默认颜色参数 / 非直角旋转或缩放）下的主要算力开销。

当前是纯软解（`decoder_node.cc` 的 `avcodec_open2` 未设置 `hw_device_ctx`），效果处理全程在 CPU，渲染时 `SDL_UpdateYUVTexture` 才把 CPU 侧 YUV 平面上传到 GPU 纹理做色彩转换和显示。GPU 化是后续独立方向，本次只处理 CPU 路径。

## Goals / Non-Goals

**Goals:**
- `MakeWritable()` 在唯一引用场景下零拷贝，消除 Color 节点的隐藏整帧拷贝
- 引入 `MediaFramePool`，让 `TransformEffectNode` 的输出帧分配可复用，消除稳态下的每帧 malloc/VirtualAlloc
- 两节点对外可观察行为（参数语义、输出尺寸、填充规则、色彩公式）不变

**Non-Goals:**
- 不做多线程/线程池分块（跨核任务级并行）
- 不做 GPU 化（shader-based color/transform），后续独立评估
- 不新增/修改 `IEffectNode` 公共接口，不改变 UI 参数面板行为
- **SIMD 向量化**：最初曾作为本次目标（`pixel_ops::ApplyLut`/`RemapPlane`/`RemapInterleavedPlane` 的 SSE2/SSSE3/AVX2 实现），实现后加了 per-frame 耗时调试日志实测，发现收益不成立（见下方"SIMD 尝试与回退"），已完整回退到纯标量实现，不再是本次目标

## Decisions

### 1. `MakeWritable()` 改为 ref-qualified 重载，唯一引用时移交所有权而非拷贝

```cpp
// media_frame.h
[[nodiscard]] MediaFrame MakeWritable() &&;       // 消费 *this：唯一引用时零拷贝
[[nodiscard]] MediaFrame MakeWritable() const &;  // 保留原对象：语义不变，总是安全拷贝
```

`&&` 重载内部先用原始 `frame_`（未额外 ref）调用 `av_frame_is_writable`：为真则 `return std::move(*this)`（真正的所有权转移，不产生新的 AVBufferRef，不存在"表面唯一实则共享"的隐患）；为假则退回现有的 `av_frame_ref` + `av_frame_make_writable` 深拷贝路径。

`const &` 重载保留原逻辑（现有行为不变，供仍需要保留原 `MediaFrame` 的调用方使用）——目前代码里没有这样的调用方，但保留是为了不破坏"总是安全"的既有语义契约。

`ColorEffectNode::Process()` 的调用点从 `input.AsFrame().MakeWritable()` 改为 `std::move(input.AsFrame()).MakeWritable()`。安全性：`input` 是按值传入 `Process()` 的局部参数，`MakeWritable()` 调用之后代码只访问 `input.timestamp()/flags()/serial()`（`MediaBuffer` 自身字段，不在被移动的 `MediaFrame` payload 内），不会读取已被移空的 `frame_`。

**考虑过的替代方案**：在现有 `const` 方法内部调整判断顺序（先查 `av_frame_is_writable(frame_.get())`，为真时 `av_frame_ref` 出一个别名返回）。放弃原因：这会让返回的"可写帧"和调用方仍持有的原 `frame_` 共享同一 buffer（refcount 变 2），"可写"的保证依赖调用方不再碰原对象这一隐式约定，而不是类型系统强制的所有权转移——不符合"消除補丁式假设"的要求。ref-qualified 重载是唯一能让编译器强制"consumed"语义的方式。

### 2. `MediaFramePool`：复用 AVBufferPool，作为 frame-abstraction 的新类型

```cpp
// media_frame.h — 与 AVFramePtr 同风格：RAII、move-only、直接持有 FFmpeg 类型
// （media_frame.h 已经通过 ffmpeg_utils.h transitively 依赖 libavutil，
//  与 AVFramePtr 的既有做法一致，不新增 Pimpl 层）
class MediaFramePool {
  public:
    MediaFramePool() = default;
    ~MediaFramePool();
    MediaFramePool(MediaFramePool&&) noexcept;
    MediaFramePool& operator=(MediaFramePool&&) noexcept;
    MediaFramePool(const MediaFramePool&) = delete;
    MediaFramePool& operator=(const MediaFramePool&) = delete;

    // 尺寸/格式变化时惰性重建内部 AVBufferPool；否则复用。
    MediaFrame Acquire(int width, int height, int format, double pts);

  private:
    AVBufferPool* pool_{nullptr};
    int width_{0};
    int height_{0};
    int format_{-1};
};
```

`Acquire()` 内部：尺寸/格式命中缓存 → `av_buffer_pool_get(pool_)` 逐平面取（或用 `av_frame_get_buffer` 接受一个复用的 buffer pool，具体取决于 FFmpeg 版本 API 的可用性，实现时以 `av_buffer_pool_init` + 手动装配 `AVFrame::buf[]/data[]/linesize[]` 为准，参照 FFmpeg 软解码器 `get_buffer2` 默认实现的同款模式）；不命中（首次调用或尺寸变化）→ `av_buffer_pool_uninit(&pool_)` 释放旧池，按新尺寸重新 `av_buffer_pool_init`。

`TransformEffectNode` 新增私有成员 `MediaFramePool output_pool_;`，`Process()` 中把 `MediaFrame::CreateSameFormat(src_mf, src_mf.pts())` 替换为 `output_pool_.Acquire(src_mf.width(), src_mf.height(), src_mf.format(), src_mf.pts())`。

**考虑过的替代方案**：
- 沿用 design.md（`effect-perf-and-frame-encap`）最初设想的节点私有 `cached_out_` 单帧复用。放弃原因：单帧复用在"上一输出帧仍被下游持有（比如 sink 还没渲染完）"时会造成写冲突，`AVBufferPool` 的多缓冲区复用（内部按引用计数决定一个 buffer 是否可被回收再利用）没有这个问题，是 FFmpeg 提供的标准机制，直接复用即符合"复用现有机制"的要求。
- 把池放到 `pixel_ops` 层。放弃原因：`pixel_ops` 被规定为零依赖纯函数集合（见 `pixel-ops` 现有 spec），引入 `AVBufferPool` 会破坏这个约束；帧生命周期管理属于 `frame-abstraction`/节点层职责。

### 3. SIMD 尝试与回退（曾实现，已完整撤销）

最初按照"镜像 FFmpeg/libyuv 运行时 CPU dispatch"的思路实现过一版：新增 `src/media/simd/cpu_features.h/.cc` 做 `HasAvx2()`/`HasSsse3()` 运行时检测；`pixel_ops_scalar.cc`/`pixel_ops_sse2.cc`/`pixel_ops_ssse3.cc`/`pixel_ops_avx2.cc` 按指令集拆分独立编译单元（MSVC 用 `/arch:AVX2` 单文件开启）；`ApplyLut` 用 nibble-split `pshufb` 技术保持"任意 256 项 LUT"的通用语义；`RemapPlane`/`RemapInterleavedPlane` 只向量化仿射坐标+双线性权重计算，texel 内存读取保留标量（当时评估 AVX2 gather 收益不确定、复杂度高，列为 Open Question）。

加了 per-frame 耗时调试日志后用真实素材实测（1080p，旋转效果，非默认参数），随后又用独立基准程序在项目实际的 Debug 编译选项（`/Od /RTC1 /MDd`）和 Release 风格选项（`/O2 /MD`）下分别测量：

| 场景 | 标量 | AVX2 |
|---|---|---|
| RemapPlane 整帧估算，Debug 编译选项 | 108.3ms | **428.0ms**（比标量慢 4 倍） |
| RemapPlane 整帧估算，Release 编译选项 | 23.2ms | 21.5ms（仅快 ~8%） |
| ApplyLut，Release 编译选项 | 0.50ms | 0.56ms（反而更慢） |

**结论**：
- Debug 编译选项（项目默认预设 `default` 就是 Debug）下，SIMD 分发引入的额外分层函数调用（分发 → 按批计算坐标 → 存栈数组 → 逐个读出调标量采样）在无内联优化时开销远超省下的坐标计算，是净回归
- 即使在 Release 编译选项下，`RemapPlane` 只向量化了坐标/权重计算（Decision 5 的原始设计），双线性采样的内存读取和混合运算仍是标量瓶颈，向量化坐标部分带来的收益被摊薄到几乎无法感知
- `ApplyLut` 是内存带宽瓶颈操作（256 项表常驻 L1），nibble-split 引入的额外 shuffle/compare/blend 运算并不能换来收益，标量版本已接近最优

因此撤销了全部 SIMD 代码（删除 `pixel_ops_scalar.cc`/`pixel_ops_sse2.cc`/`pixel_ops_ssse3.cc`/`pixel_ops_avx2.cc`/`src/media/simd/`），`pixel_ops.cc` 恢复为单一标量实现，只保留一个不涉及 SIMD、纯代数上等价的优化：按行预计算 `RowOffsets`（利用同一行内 `sx`/`sy` 关于 `x` 为线性函数的恒等式），避免逐像素重新展开完整仿射公式。详细数据和分析记录在 `docs/improvements/pixel-ops-simd.md`，供以后重新评估 SIMD 时参考（例如先解决 Debug 配置下的内联问题，再决定是否值得做真正的 gather+blend 向量化）。

## Risks / Trade-offs

- **[Risk] `MediaFramePool::Acquire` 的 AVBufferPool 手动装配 `AVFrame` 字段容易出错（linesize 对齐、data[] 指针切分）** → Mitigation：实现时对照 FFmpeg `av_frame_get_buffer` 的内部逻辑（`libavutil/frame.c`）做字段装配，已通过实际播放验证
- **[Risk] `MakeWritable() &&` 重载如果未来有新调用方误用 lvalue 版本（每次都拷贝）而没注意到有更优的 rvalue 版本** → Mitigation：`[[nodiscard]]` 保留在两个重载上，后续 code review 按 `frame-abstraction` spec 的新增 scenario 检查调用点是否具备"消费即所有权转移"的条件

## Open Questions

- 是否需要线程池做跨核并行？本次明确排除（见 Non-Goals），但如果内存分配修复后 4K 仍不达标，是下一个自然的候选项
- `RemapPlane` 的 texel gather 是否值得在未来上 AVX2 gather 并同时向量化混合运算？本轮已验证仅向量化坐标部分收益不大，若后续重新考虑 SIMD，需要先解决 Debug 编译配置下的内联问题（见 `docs/improvements/pixel-ops-simd.md`），否则很难在实际开发环境中验证收益
- `MediaBuffer` 构造函数中 `media_type_(frame.type())` 在成员初始化列表里于 `payload_(std::move(frame))` 之后求值，读到的是 move 后的 `frame`（`type_` 已被重置为 `kUnknown`）——这是本次排查中顺带发现的疑似既有 bug，与本次改动无关，不在本次范围内修复，已记录到 `docs/improvements/pipeline-interface-design.md`
