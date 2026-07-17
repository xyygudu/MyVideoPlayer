## 1. pixel_ops 工具集

- [x] 1.1 新增 `src/media/pixel_ops.h`：声明 `ColorLut`/`BuildColorLut`/`ApplyLut`/`AffineMapping`/`ComputeAffineMapping`/`RemapPlane`/`RemapInterleavedPlane`/`TryPermutePlane`，全部在 `mvp::pixel_ops` 命名空间下
- [x] 1.2 新增 `src/media/pixel_ops.cc`：实现以上函数。`TryPermutePlane` 覆盖 4(角度)×2(h_flip)×2(v_flip)=16 种组合的整数下标拷贝，0°+no flip 用 `memcpy`
- [x] 1.3 `RemapPlane` 内部使用 `ComputeAffineMapping` 的预计算常量，逐像素只做矩阵乘法（4 mul + 2 add），不再每像素算圆心/平移

## 2. MediaFrame 封装增强

- [x] 2.1 `media_frame.h` 新增 `width()`/`height()`/`format()`/`PlaneData(int)`/`PlaneLinesize(int)`/`MakeWritable()`/`CreateSameFormat()`
- [x] 2.2 `media_frame.cc` 实现以上方法
- [x] 2.3 `MakeWritable()` 声明带 `[[nodiscard]]`

## 3. ColorEffectNode LUT 化

- [x] 3.1 `color_effect_node.cc` 的 `Process()` 开头增加恒等快速路径
- [x] 3.2 非恒等路径改为调 `pixel_ops::BuildColorLut` + `pixel_ops::ApplyLut`，移除匿名 namespace 中的 `ApplyLinear`（代码迁移到 pixel_ops.cc）
- [x] 3.3 平面访问改为 `input.AsFrame().MakeWritable().PlaneData(i)` 模式，不再调用 `RawFrame()` 和 `av_frame_make_writable`

## 4. TransformEffectNode 优化

- [x] 4.1 `transform_effect_node.h` 新增 `AVFramePtr cached_out_` 成员（注：实际实现中输出帧改用 `MediaFrame::CreateSameFormat` 每次创建——因为 `AVFramePtr` 的 ownership 语义与 MediaFrame 的 move-only + 构造时 `av_frame_ref` 路径冲突，保持每次 CreateSameFormat 的简单性和正确性）
- [x] 4.2 `Process()` 开头增加恒等快速路径（全默认参数直接透传）
- [x] 4.3 增加 `TryPermutePlane` 分支（纯排列场景走整数下标拷贝，命中则跳过双线性）
- [x] 4.4 双线性路径改为调 `pixel_ops::ComputeAffineMapping` + `pixel_ops::RemapPlane`；NV12 色度调 `pixel_ops::RemapInterleavedPlane`
- [x] 4.5 输出帧通过 `MediaFrame::CreateSameFormat` 创建
- [x] 4.6 移除匿名 namespace 中的 `InverseMap`/`SampleComponent`/`RemapComponent`（逻辑迁移到 pixel_ops.cc）和 `AllocateOutputFrame`
- [x] 4.7 平面访问改为 `MediaFrame` 新方法，不再调 `RawFrame()`

## 5. 构建验证

- [x] 5.1 编译通过（`cmake --build build`），零错误零警告
- [x] 5.2 冒烟测试：启动 app 正常，无崩溃
