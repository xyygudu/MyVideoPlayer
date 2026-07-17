## MODIFIED Requirements

### Requirement: ColorEffectNode 调整亮度对比度饱和度
系统 SHALL 定义 `ColorEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），对 YUV 类帧的 Y 平面做亮度/对比度线性变换，对 U/V 平面做饱和度缩放。

ColorEffectNode SHALL 提供参数（均为 `kFloat`）：`brightness`（默认 0.0）、`contrast`（默认 1.0）、`saturation`（默认 1.0）。

`Process()` SHALL 在执行任何像素操作之前检查恒等路径：若 `brightness==0 && contrast==1.0 && saturation==1.0`，直接透传，不进行任何像素读写。

对非恒等路径，`Process()` SHALL 调用 `pixel_ops::BuildColorLut` 预计算 Y 和 UV 各一个 256 项 `uint8_t` 查找表，然后调用 `pixel_ops::ApplyLut` 分别处理 Y/U/V 平面。

若输入帧的 pixel format 不在 `IsPlanarYuvPixelFormat` 支持的范围内，SHALL 记录一次 spdlog 警告并透传。

所有平面数据访问 SHALL 通过 `MediaFrame::PlaneData()`/`PlaneLinesize()` 完成，不使用 `RawFrame()` 或任何 `av_frame_*` 函数。

#### Scenario: LUT 路径与浮点路径结果一致
- **WHEN** `brightness=0.2, contrast=1.0, saturation=1.0`，Y 平面某像素 Y=100
- **THEN** 输出 Y 值 ≈ 151（与浮点 `ApplyLinear` 结果误差 ≤1，仅四舍五入差异）

#### Scenario: 默认参数走恒等快速路径
- **WHEN** brightness=0, contrast=1.0, saturation=1.0
- **THEN** Process() 直接透传输入帧，不读取任何平面数据

### Requirement: TransformEffectNode 合并几何变换，支持任意角度旋转
系统 SHALL 定义 `TransformEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），将旋转、水平翻转、垂直翻转、缩放、平移合并为一次像素重映射处理。

TransformEffectNode SHALL 提供以下参数：
- `rotate_deg`（`kFloat`）：任意角度（0.0~360.0），默认 0.0
- `flip_h` / `flip_v`（`kBool`），默认 false
- `scale_x` / `scale_y`（`kFloat`），默认 1.0
- `translate_x` / `translate_y`（`kFloat`），默认 0.0

`Process()` SHALL 优先尝试 `pixel_ops::TryPermutePlane` 纯排列路径（当 rotate 为 0/90/180/270 且 scale=1.0 且 translate=0 时），命中则跳过双线性插值。未命中时调用 `pixel_ops::ComputeAffineMapping` 预计算映射常量，走 `RemapPlane`/`RemapInterleavedPlane`（NV12 色度用后者以避免重复 InverseMap）。

输出帧尺寸 SHALL 恒等于输入帧尺寸。

TransformEffectNode SHALL 缓存输出帧内存，仅在输入帧尺寸或格式变化时才重新分配。

所有平面数据访问 SHALL 通过 `MediaFrame::PlaneData()`/`PlaneLinesize()` 完成，不使用 `RawFrame()` 或任何 `av_frame_*` 函数。

#### Scenario: 纯排列场景走 TryPermutePlane
- **WHEN** rotate_deg=90, flip_h=false, scale=1.0, translate=0
- **THEN** 调用 TryPermutePlane 返回 true，不进入双线性插值分支

#### Scenario: NV12 色度不重复计算 InverseMap
- **WHEN** 输入帧格式为 NV12 且走双线性路径
- **THEN** 调用 RemapInterleavedPlane 一次，其内部只执行一次仿射逆运算同时写入 U 和 V
