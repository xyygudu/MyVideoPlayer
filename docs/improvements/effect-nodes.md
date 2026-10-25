# 特效节点待改进点

> 记录时间：2026-09-30
> 对标参考：FFmpeg (libavutil/pixdesc.h `AVComponentDescriptor.step`)、VLC (`plane_t` 的 `i_pixel_pitch` / `i_visible_pitch`)
> 关联：change gpu-stage-a-hw-decode-render 验收 6.7

---

## 1. ColorEffectNode 对 NV12 交错色度平面只处理左半行

> **已解决（2026-09-30，change gpu-stage-a-hw-decode-render）**：`ChromaPlaneLayout` 增加 `row_bytes`，`ColorEffectNode` 改用它做逐字节 LUT。当时项目没有单元测试框架，靠实机验证；现在 `mvp_tests` 的 `Paths/EffectPathTest.*/CpuOnDownloadedFrames` 覆盖了 NV12 的 CPU 特效路径（2026-10-25，change player-robustness-and-debug-settings）。下文保留作为问题记录。

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

---

## 2. GPU pass 每帧创建 SRV/RTV

> 记录时间：2026-10-01，关联 change gpu-stage-b-gpu-effects

### 问题

`D3D11VideoPass::Run` 每帧为源帧创建两个 SRV、为输出帧创建两个 RTV，用完即释放。

### 影响场景

- **4K60 双特效**：每秒约 480 次视图创建，驱动调用微秒级，目前可接受
- **特效链变长**：开销随特效数线性增长

### 改进建议（参考 GStreamer `GstD3D11Memory`）

GStreamer 把 SRV/RTV 缓存在内存对象上、随内存对象释放。本项目可对 pass 输出帧池（纹理有限且复用）按纹理指针缓存视图；解码数组纹理按 (纹理, 切片) 缓存，并在解码帧池重建（每次 seek）时整体清空，避免缓存的视图把旧纹理数组钉住。

---

## 3. 尚无图级域转换节点自动插入

### 问题

特效节点靠"双实现 + 按帧域分派"保持链路域不变（硬件进硬件出、软件进软件出），GPU 不可用时在节点内部下载。这要求每个特效都同时有 CPU 与 GPU 实现。

### 影响场景

- **只有 CPU 实现的特效**（如依赖第三方 CPU 库的分析类特效）：放在两个 GPU 特效之间时会把链路打回内存，且下一个 GPU 特效拿到的是软件帧，只能走它自己的 CPU 路径
- **只有 GPU 实现的特效**：软解链路上没有上传路径

### 改进建议（参考 GStreamer caps feature `memory:D3D11Memory` + `d3d11upload/d3d11download`）

节点如实声明各端口接受/产出的帧域，协商时在域不匹配的连接上自动插入 upload/download 转换节点，特效节点内部不再隐式下载。

---

## 4. 变换特效的像素几何在 CPU/GPU 三条路径上不一致

> **已解决（2026-10-25，change player-robustness-and-debug-settings）**：中心改为 (n−1)/2（CPU 与着色器同时改）；置换只处理精确的四分之一圈并支持分量偏移；NV12 双线性改用色度平面自己的映射。`EffectPathTest.PausedFlipRedrawsAnExactMirror/*` 与 `TransformLooksTheSameOnEveryPath` 逐路径比对。下文保留作为问题记录。

### 问题

给设置页加上「GPU 特效」开关、并用截图逐路径比对后发现四处缺陷：

1. **中心偏半像素**：`ComputeAffineMapping` 与 `affine_remap_ps.hlsl` 都以整数坐标表示像素中心，却把中心取成 `n / 2`。镜像变成 `x → n − x`：GPU 路径第 0 列采样越界成黑线，整幅画面错位 1 像素（色度 2 像素）
2. **任意角度被量化**：`TryPermutePlane` 把所有角度量化到 0/90/180/270 后走置换，只设置了旋转的帧在 CPU 上「旋转 20° = 不动」
3. **NV12 置换只拷 U**：`comp_offset != 0` 直接返回 false，V 分量保留帧池里的旧内容
4. **NV12 双线性用了亮度映射**：交错色度平面按亮度尺寸的中心与平移量重采样，色度与亮度错位

### 影响场景

- **硬解 + GPU 特效**：翻转/旋转后画面边缘出现黑线，整体偏移
- **硬解 + CPU 特效**（关闭 GPU 特效、或 GPU pass 失败回退）：翻转后颜色错误（V 是垃圾数据），小角度旋转无效
- **开关 GPU 特效对照调试**：同一参数两条路径结果不同，无法用对照定位问题

### 改进建议（参考 FFmpeg `vf_transpose`/`vf_hflip` 与 OpenGL/D3D 的像素中心约定）

FFmpeg 的 `hflip` 直接写 `dst[x] = src[w - 1 - x]`，只对精确的四分之一圈用 `transpose` 置换，任意角度交给 `rotate` 滤镜重采样。D3D/GL 的纹理坐标里像素中心在 `(i + 0.5) / n`，换算到整数中心坐标系时平面中心是 `(n − 1) / 2`。两条路径共享一个约定，并用自动化测试逐路径比对输出。

---

## 5. 4:2:2 方形画面旋转 90° 时色度未被处理

### 问题

`TryApplyPermute` 只用亮度平面的返回值决定是否走置换。yuv422p 的方形画面亮度是方的、色度（w/2 × h）不是，色度的 `TryPermutePlane` 返回 false 被忽略，输出帧的色度平面没有被写入。

### 影响场景

- **4:2:2 且宽高相等的素材旋转 90°/270°**（极少见，如专业采集的方形画幅）：颜色为帧池里的旧内容

### 改进建议（参考 FFmpeg `vf_transpose` 的逐平面决策）

置换必须对所有平面都成立才走置换：先对全部平面做可行性判断，任一平面不行就整帧走双线性；或按 FFmpeg `transpose` 的做法，90° 旋转输出宽高互换的帧而不是裁成原尺寸。

