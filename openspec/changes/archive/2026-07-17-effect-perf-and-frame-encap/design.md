## Context

`TransformEffectNode` 和 `ColorEffectNode` 当前每像素执行标量浮点运算，1080p 下 transform ~47ms/帧、color ~6ms/帧，两项叠加远超 30fps 的 33ms 预算。像素运算逻辑散落在两个节点的匿名 namespace 中，且通过 `AVFrame*` 裸指针直接操作 FFmpeg 帧的内部结构（`data[]`/`linesize[]`/`width`/`height`），调用 `av_frame_make_writable`/`av_frame_get_buffer` 管理引用计数——这些属于 FFmpeg 内部细节，不应出现在 node 层。

`MediaFrame` 当前只暴露 `RawFrame()` 一个数据访问入口（返回裸 `AVFrame*`），形同虚设的封装。

## Goals / Non-Goals

**Goals:**
- 新增 `pixel_ops` 纯函数工具集（LUT/仿射映射/双线性采样/平面重映射），零依赖，对标 FFmpeg `libavutil` 模式
- ColorEffectNode 用 256 项 LUT 替代逐像素浮点（~5x 加速）
- TransformEffectNode 增加恒等/纯排列快速路径（0/90/180/270° + 翻转 → 整数下标重排）、NV12 色度去重、帧级常量提升、输出帧缓存
- `MediaFrame` 新增 `PlaneData()`/`PlaneLinesize()`/`width()`/`height()`/`format()`/`MakeWritable()`/`CreateSameFormat()`，effect node 不再直接调 `RawFrame()` 或任何 `av_frame_*`

**Non-Goals:**
- 不做 SIMD（SSE/AVX），留作后续独立优化
- 不引入 libavfilter
- `pixel_ops` 不暴露任何类/虚方法——纯自由函数 + namespace
- 不删除 `MediaFrame::RawFrame()`（向后兼容）

## Decisions

### 1. `pixel_ops` 命名空间：纯自由函数 + 命名参数结构体

```cpp
// src/media/pixel_ops.h — 唯一依赖：<cstdint>
namespace mvp::pixel_ops {

struct ColorLut { uint8_t y[256]; uint8_t uv[256]; };
ColorLut BuildColorLut(float brightness, float contrast, float saturation);
void ApplyLut(uint8_t* plane, int linesize, int width, int height, const uint8_t lut[256]);

struct AffineMapping {
    float inv[4];  // 逆矩阵 2×2（行主序）
    float cx, cy;  // 目标平面中心
    float tx, ty;  // 归一化平移（像素单位）
};
AffineMapping ComputeAffineMapping(const TransformAffineParams& p, int w, int h);

// 双线性重映射主循环。comp_stride/comp_offset 让平面 Y/U/V 和 NV12 交织分量共用同一函数。
void RemapPlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                int width, int height, int comp_stride, int comp_offset,
                uint8_t fill, const AffineMapping& m);

// NV12 色度专用：一次逆映射同时写入 U(offset=0) 和 V(offset=1)。
void RemapInterleavedPlane(const uint8_t* src, int src_linesize,
                           uint8_t* dst, int dst_linesize,
                           int width, int height, uint8_t fill, const AffineMapping& m);

// 纯排列重排（无插值）：覆盖旋转±90°/180°、镜像、无缩放平移的整数下标拷贝。
// rotate_deg 会被量化到 {0,90,180,270}，非直角值不命中此路径。
bool TryPermutePlane(const uint8_t* src, int src_linesize, uint8_t* dst, int dst_linesize,
                     int width, int height, int comp_stride, int comp_offset,
                     const TransformAffineParams& p);

}  // namespace mvp::pixel_ops
```

`TransformAffineParams` 维持在 `transform_effect_node.h` 中（是节点级的参数快照，有具体语义），`pixel_ops` 接收它作为输入——这是"上层传参数，底层干像素活"的正向依赖。

`RemapInterleavedPlane` 只在 NV12 源的 `Process()` 中调用，yuv420p 等 planar 格式仍走两次 `RemapPlane`。不做格式分发到 `pixel_ops` 内部的原因：格式判断（`ComputeChromaPlaneLayout`）和循环调度逻辑属于节点层的职责，`pixel_ops` 应当是纯像素处理的机械部件。

### 2. ColorEffectNode LUT 转换

`BuildColorLut` 在 `Process()` 开头调用一次（~5000 次浮点），三个平面各调用一次 `ApplyLut`。核心循环：

```cpp
// pixel_ops.cc
void ApplyLut(uint8_t* plane, int linesize, int width, int height, const uint8_t lut[256]) {
    for (int y = 0; y < height; ++y) {
        uint8_t* row = plane + y * linesize;
        for (int x = 0; x < width; ++x) row[x] = lut[row[x]];
    }
}
```

恒等快速路径：`brightness==0 && contrast==1.0 && saturation==1.0` 时直接 `emit(std::move(input))`。

### 3. TransformEffectNode 快速路径：`TryPermutePlane`

覆盖不涉及双线性插值的"纯排列重排"场景。条件：`rotate_deg ∈ {0,90,180,270}`，`scale_x==scale_y==1.0`，`translate==0`。翻转可以任意组合——翻转只是改变遍历方向和符号，不需要插值。

每种 (旋转角度, flip_h, flip_v) 组合对应一种"源坐标→目标坐标"的整数映射函数。因为只有 4×2×2=16 种组合，手写 16 个针对性的内循环即可，不引入通用映射表。

恒等（0°+no flip）为最高频场景，直接逐行 `memcpy`。

### 4. TransformEffectNode 帧级常量提升

`ComputeAffineMapping` 预计算 `inv[4]`（逆矩阵 2×2）、`cx/cy`（平面中心）、`tx/ty`（归一化平移像素化后的偏移）。`RemapPlane` 内部逐像素只执行 `dst_x→src_x` 的矩阵乘法（4 次乘法+2 次加法），不再每像素重算圆心和偏移。

### 5. TransformEffectNode 输出帧缓存

`TransformEffectNode` 新增成员 `AVFramePtr cached_out_`。`Process()` 中：

```cpp
if (!cached_out_ || cached_out_->width != src->width ||
    cached_out_->height != src->height || cached_out_->format != src->format) {
    cached_out_.unref();
    av_frame_get_buffer(cached_out_.get(), 0);  // 仅尺寸/格式变化时分配
    cached_out_->width = src->width;  // av_frame_get_buffer 会覆写宽高，需要补回
    ...
}
av_frame_ref(dst, cached_out_.get());  // 复用同一块内存
```

### 6. `MediaFrame` 新增方法

```cpp
class MediaFrame {
  public:
    // --- 新增 ---
    int width() const;                   // return frame_->width
    int height() const;                  // return frame_->height
    int format() const;                  // return frame_->format (AVPixelFormat)
    const uint8_t* PlaneData(int p) const;
    uint8_t* PlaneData(int p);           // 需 MakeWritable() 后的帧
    int PlaneLinesize(int p) const;      // return frame_->linesize[p]

    MediaFrame MakeWritable() const;     // av_frame_is_writable → return *this (move);
                                         // 否则深拷贝一份返回

    static MediaFrame CreateSameFormat(const MediaFrame& ref, double pts);
                                         // 同尺寸/格式空帧，不拷贝像素
};
```

`MakeWritable()` 返回值语义对标 GStreamer `gst_buffer_make_writable()`：不修改当前对象（`const`），返回一个明确可写的副本。调用方拿到返回值后放心改。

### 7. 依赖方向

```
pixel_ops.h     → 依赖 <cstdint>（纯数据操作，零 FFmpeg）
ffmpeg_utils.h  → 依赖 libavutil（AVFrame 工具、ChromaPlaneLayout 等辅助结构体）
media_frame.h   → 依赖 ffmpeg_utils.h（持有 AVFramePtr）
effect nodes    → 依赖 media_frame.h + pixel_ops.h + ffmpeg_utils.h
```

`IsPlanarYuvPixelFormat`/`ComputeChromaPlaneLayout` 保留在 `ffmpeg_utils.h`（需要 `AV_PIX_FMT_*` 常量，天然有 FFmpeg 依赖）。

## Risks / Trade-offs

- **LUT 对超出 [-1,1] 范围的 brightness 值会钳位**：`BuildColorLut` 内部对每个 0-255 输入调用 `ApplyLinear`，钳位到 [0,255]。如果未来参数范围扩展导致 LUT 不再能表达（如 brightness 很大使某些输入超出 255 后再落到 255 以内），LUT 不丢失精度——它只是浮点函数在 256 个离散点上的查表等价。
- **`TryPermutePlane` 覆盖 16 种组合**：如果未来加了新的翻转轴或新旋转模式，需要补分支，但当前参数集已覆盖所有 C++ 层面可能的组合。
- **`MakeWritable()` 的 `const` + 返回值语义**：如果调用方忘记接收返回值（`mf.MakeWritable();` 而不是 `auto w = mf.MakeWritable();`），编译器会警告（`[[nodiscard]]`），且原始帧不受影响——不会静默写出错。
- **`MediaFrame` 新增方法暴露了 `AVPixelFormat` 裸 int**：`format()` 返回 `int` 而非项目内 `PixelFormat` 枚举，因为 FFmpeg 在解码后才确定真实 pixel format（`DecoderNode` 的 `Negotiate()` 阶段放的只是占位值）。节点内部用 `IsPlanarYuvPixelFormat(frame->format)` 做判断，不依赖项目枚举的精确性。

## Open Questions

- `TryPermutePlane` 的 16 分支是否需要单元测试覆盖所有组合？建议先上主要 4 条（0°、90°、180°、0°+flip_h），其余在代码审查时目视验证。
