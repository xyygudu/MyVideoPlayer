# 特效节点待改进点

> 记录时间：2026-09-30
> 对标参考：FFmpeg (libavutil/pixdesc.h `AVComponentDescriptor.step`)、VLC (`plane_t` 的 `i_pixel_pitch` / `i_visible_pitch`)
> 关联：change gpu-stage-a-hw-decode-render 验收 6.7

---

## 1. ColorEffectNode 对 NV12 交错色度平面只处理左半行

> **已解决（2026-09-30，change gpu-stage-a-hw-decode-render）**：`ChromaPlaneLayout` 增加 `row_bytes`，`ColorEffectNode` 改用它做逐字节 LUT。项目暂无 C++ 单元测试框架，下文建议的单元测试未落地，靠实机验证。下文保留作为问题记录。

### 问题

复现：`tearsofsteel_4k.mov` 走 D3D11VA 硬解，调低饱和度后只有画面左半边变灰，右半边保持原色。

`ChromaPlaneLayout.width` 对 NV12 的约定是**分量对数**（每行 U/V 样本数 = 亮度宽/2），不是字节数；调用方需要自己按步长 2 取 U/V。`TransformEffectNode` 遵守了这个约定（`RemapInterleavedPlane` 按 `x * 2` 寻址，`TryPermutePlane` 用 `width * comp_stride` 算行字节），但 `ColorEffectNode::Process` 直接把它当字节数交给 `ApplyLut`：

```cpp
ChromaPlaneLayout layout = ComputeChromaPlaneLayout(mf.format(), mf.width(), mf.height());
int plane_count = layout.interleaved ? 1 : 2;
for (int p = 0; p < plane_count; ++p) {
    pixel_ops::ApplyLut(mf.PlaneData(1 + p), mf.PlaneLinesize(1 + p),
                        layout.width, layout.height, lut.uv);  // NV12: 只处理了 w/2 字节 = 左半幅
}
```

NV12 的 UV 行实际有 `w/2 对 × 2 字节 = w` 字节，LUT 只覆盖了前一半。

根本问题在于 `ChromaPlaneLayout` 的 `width` 语义随格式变化（平面格式 = 字节数，交错格式 ≠ 字节数），调用方必须记住这个差别，漏一处就出错。

### 影响场景

- **所有硬解视频 + 饱和度调节**：硬件帧在特效节点边界下载后是 NV12，必现。日志可区分：`actual output format ... domain=6`（D3D11）的片源受影响，`domain=1 av=0`（软解 YUV420P）的片源正常，这就是「其他视频好像正常」的原因
- **亮度/对比度不受影响**：只作用于 Y 平面，Y 平面宽度即字节数
- **暴露时机**：change gpu-stage-a 之前解码器输出固定为 YUV420P，NV12 从未到达特效节点；task 4.2 引入硬件帧下载后该分支才第一次被走到

### 改进建议（参考 FFmpeg `AVComponentDescriptor.step` / VLC `plane_t.i_visible_pitch`）

FFmpeg 和 VLC 都把「一行有多少有效字节」作为平面描述的一等字段，而不是让调用方从样本数推算：FFmpeg 由 `av_image_fill_linesizes` / 分量 `step` 给出，VLC 的 `plane_t` 同时携带 `i_pixel_pitch`（每样本字节）和 `i_visible_pitch`（每行可见字节）。

在 `ChromaPlaneLayout` 中补上字节维度，逐字节操作的调用方使用它：

```cpp
struct ChromaPlaneLayout {
    bool interleaved{false};
    int width{0};        // samples per component per row
    int height{0};
    int row_bytes{0};    // visible bytes per row: width * (interleaved ? 2 : 1)
};

// ColorEffectNode：LUT 对 U/V 相同，交错平面整行一次处理即可
pixel_ops::ApplyLut(mf.PlaneData(1 + p), mf.PlaneLinesize(1 + p),
                    layout.row_bytes, layout.height, lut.uv);
```

同时补一个单元测试：构造 NV12 帧，饱和度 0 处理后断言整行 UV 字节均为 128。
