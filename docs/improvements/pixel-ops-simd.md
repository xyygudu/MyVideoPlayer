# pixel_ops SIMD 尝试与撤销记录

> 记录时间：2026-07-24
> 关联变更：openspec/changes/effect-alloc-simd-opt

---

## 1. SIMD 向量化 ApplyLut/RemapPlane 收益不成立

### 问题

为 `pixel_ops::ApplyLut`（Color 节点的亮度/对比度/饱和度 LUT 应用）和 `pixel_ops::RemapPlane`/`RemapInterleavedPlane`（Transform 节点的双线性重映射）实现了运行时 CPU 特性分发的 SIMD 版本：

- `ApplyLut`：SSSE3/AVX2 nibble-split `pshufb` 技术，把 256 项 LUT 按高 4 位切成 16 组，逐组 `pshufb` + 比较混合
- `RemapPlane`/`RemapInterleavedPlane`：向量化仿射逆映射坐标 + 双线性权重计算（4 像素/批 SSE2，8 像素/批 AVX2），texel 实际内存读取和混合运算保留标量（未做 gather 向量化）

实现后加了 per-frame 耗时调试日志实测，又用独立基准程序在项目实际 Debug 编译选项（`/Od /RTC1 /MDd`）和 Release 风格选项（`/O2 /MD`）下分别测量：

| 场景 | 标量 | AVX2 |
|---|---|---|
| RemapPlane 整帧估算，Debug 编译选项 | 108.3ms | **428.0ms**（比标量慢 4 倍） |
| RemapPlane 整帧估算，Release 编译选项 | 23.2ms | 21.5ms（仅快 ~8%） |
| ApplyLut，Release 编译选项 | 0.50ms | 0.56ms（反而更慢） |

### 影响场景

- **项目默认开发配置是 Debug**（`CMakePresets.json` 的 `default` preset，`CMAKE_BUILD_TYPE=Debug`）：SIMD 分发引入的额外分层调用（分发入口 → 按批计算坐标 → 存到栈数组 → 逐个读出调标量采样）在 `/Od` 下完全不会被内联，比原来的单一标量函数开销更大——**这是一次实打实的性能回归，不是"收益不够大"而是"更慢"**
- **即使在 Release 优化下**，`RemapPlane` 的收益也只有 ~8%：双线性采样的内存读取（4 个 texel）和混合运算本身是标量，才是真正的开销大头，只向量化坐标计算这一小部分自然摊薄不出明显收益
- **`ApplyLut` 是内存带宽瓶颈操作**：256 项 LUT 常驻 L1 缓存，标量版本已经接近内存带宽上限，nibble-split 引入的 shuffle/compare/blend 额外算术开销纯粹是负担

### 改进建议（参考 FFmpeg/libyuv/x264 的 SIMD 实践）

如果未来重新评估 SIMD：

1. **先解决 Debug 编译配置下的内联问题**，再评估收益，否则测出来的数字会具有误导性。可选方向：
   - 项目目前只有 Debug 一个可用的开发配置（`build-release` 尝试时发现 `D:\Install\spdlog` 只装了 Debug 版的库，`spdlogd.lib` 缺对应的 Release 变体），要跑通真正的 Release 构建需要先解决这个依赖问题
   - 或者接受"性能敏感的像素处理代码在 Debug 下就是要慢很多"这个现实，改用其他方式做性能验证（比如本文档里用的：绕开 CMake，直接用 `cl.exe` + 明确的 `/O2` 编一个独立基准程序）

2. **双线性重映射如果要做 SIMD，必须连 texel gather 和混合运算一起向量化**（AVX2 `_mm256_i32gather_epi32` 或手写并行 gather），只做坐标计算的"半吊子向量化"意义不大——本次的 8% 提升就是明证。这和 FFmpeg `libswscale`、mpv 的 GPU shader 采样路径的思路一致：真正的收益来自把整个采样+混合流程搬到向量/并行单元，而不是外围的标量补丁

3. **LUT 应用类操作（内存带宽瓶颈）大概率不值得上 SIMD**，除非 LUT 本身很大（超出 L1/L2）或者同时对多个平面做流水线处理以掩盖延迟。单纯 256 项字节映射，标量已经是好的基线

4. **双线性路径如果真要提速，更值得投入的方向**：
   - 先确认瓶颈是否仍在这里（本次的内存分配修复—— `MakeWritable` 零拷贝 + `MediaFramePool`——已经解决了另一大块开销，`pool_us` 实测已降到个位数微秒）
   - 线程池按行/按平面分块的跨核并行（本次 Non-Goal，效果不确定但方向和 SIMD 正交，理论上限更高）
   - 更彻底地考虑 GPU 化（mpv/vlc 的 GPU shader 路径，双线性采样直接吃硬件纹理单元，不需要手写 SIMD）

### 结论

最终决定：撤销 `pixel_ops` 的 SIMD 分发，`ApplyLut`/`RemapPlane`/`RemapInterleavedPlane` 保持纯标量实现（保留了一个和 SIMD 无关的纯代数优化：按行预计算 `RowOffsets`，利用同一行内 `sx`/`sy` 关于 `x` 为线性函数的恒等式，避免逐像素重新展开完整仿射公式）。SIMD 相关代码（`pixel_ops_scalar.cc`/`pixel_ops_sse2.cc`/`pixel_ops_ssse3.cc`/`pixel_ops_avx2.cc`/`src/media/simd/`）已从代码库中删除，`openspec/changes/effect-alloc-simd-opt` 的 `specs/pixel-ops/spec.md` 也已删除（该行为从未真正对外生效）。
