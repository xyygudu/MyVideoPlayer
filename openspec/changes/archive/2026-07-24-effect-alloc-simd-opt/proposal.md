## Why

4K 播放时 `TransformEffectNode`/`ColorEffectNode` 的 CPU 效果处理路径明显卡顿。排查发现瓶颈不止在像素运算本身：`ColorEffectNode` 每次调用 `MediaFrame::MakeWritable()` 都会因实现缺陷（先 `av_frame_ref` 再判断可写性，人为把 refcount 顶到 2）触发一次不必要的整帧深拷贝，即便输入帧本来就是唯一持有；`TransformEffectNode` 每帧通过 `MediaFrame::CreateSameFormat` 走 `av_frame_get_buffer` 做全新分配，4K 下单帧 ~12MB 的分配容易命中系统级大内存路径，造成分配抖动（卡顿而非均匀变慢）。此外，`pixel_ops` 的双线性重映射和 LUT 应用仍是逐像素标量实现，在非直角旋转/缩放场景下是主要的算力瓶颈。这两类问题（内存分配开销、标量像素运算）在 4K 下都会被放大，需要在继续做 GPU 化之前先把 CPU 路径的这两处短板补上。

## What Changes

- 修复 `MediaFrame::MakeWritable()`：唯一引用（`av_frame_is_writable` 为真）时零拷贝移交所有权，不再强制深拷贝；仍非唯一引用时保留原有深拷贝回退路径
- 新增 `MediaFramePool`（frame-abstraction 能力下的新类型）：为 `TransformEffectNode` 这类"每帧需要一块同尺寸/格式输出缓冲区"的场景提供可复用的 `AVBufferPool` 封装，避免每帧 `av_frame_get_buffer` 触发的系统级分配；尺寸/格式变化时才重建池
- `TransformEffectNode::Process()` 改用 `MediaFramePool` 获取输出帧，替换现有的直接 `MediaFrame::CreateSameFormat` 调用
- 两个效果节点的可见行为（参数含义、输出尺寸、色彩公式、填充规则等）保持不变，本次改动都是内部实现优化，不改变对外可观察的像素结果

**已尝试并回退**：`pixel_ops` 曾实现 SSE2/SSSE3/AVX2 三级运行时分发（LUT nibble-split pshufb + 向量化仿射坐标计算）。加了per-frame耗时调试日志后实测：项目默认 Debug 编译配置（`/Od /RTC1`）下 AVX2 版本比标量慢 4 倍（分发/批处理引入的额外函数调用和栈读写在无内联优化时开销远超省下的计算）；即使在 Release 编译选项下也只有 ~8% 提升（双线性路径的瓶颈在纹理采样和混合运算，这次向量化只覆盖了坐标计算部分）；LUT 路径的 SIMD 版本在 Release 下反而比标量慢（LUT 本身是内存带宽瓶颈操作，标量版本已接近最优）。收益不确定且有 Debug 环境下的回归风险，已完整回退到标量实现，细节记录在 `docs/improvements/pixel-ops-simd.md`。

## Capabilities

### New Capabilities

（无新增独立能力；`MediaFramePool` 作为 `frame-abstraction` 能力的扩展，不单独立项）

### Modified Capabilities

- `frame-abstraction`: `MakeWritable()` 唯一引用时不再深拷贝；新增 `MediaFramePool` 类型及其复用分配语义

## Impact

- `src/media/media_frame.h` / `.cc`：`MakeWritable()` 行为修正；新增 `MediaFramePool` 类
- `src/media/pixel_ops.cc`：`RemapPlane`/`RemapInterleavedPlane` 增加按行预计算 `RowOffsets` 的标量优化（非 SIMD，纯代数化简）
- `src/media/nodes/transform_effect_node.h` / `.cc`：改用 `MediaFramePool` 而非 `MediaFrame::CreateSameFormat`
- `src/media/nodes/color_effect_node.cc`：`MakeWritable()` 调用点改为按值消费（`std::move`）以命中零拷贝路径
- 不影响：`IEffectNode` 公共接口、UI 参数面板、graph 节点生命周期/线程模型、`pixel_ops` 公开函数签名
