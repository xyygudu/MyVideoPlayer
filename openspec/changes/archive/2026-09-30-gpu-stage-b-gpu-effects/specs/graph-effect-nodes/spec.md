## MODIFIED Requirements

### Requirement: TransformEffectNode 合并几何变换，支持任意角度旋转
系统 SHALL 定义 `TransformEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），将旋转、水平翻转、垂直翻转、缩放、平移合并为一次像素重映射处理，避免多趟遍历同一帧。

TransformEffectNode SHALL 提供以下参数：
- `rotate_deg`（`kFloat`）：任意角度（0.0~360.0，连续值），默认 0.0
- `flip_h` / `flip_v`（`kBool`）：是否水平/垂直翻转，默认 false
- `scale_x` / `scale_y`（`kFloat`）：缩放比例，默认 1.0
- `translate_x` / `translate_y`（`kFloat`）：归一化平移比例（相对帧宽高，-1.0~1.0），默认 0.0

TransformEffectNode SHALL 先在画面自身坐标系中做翻转与缩放，再绕画面中心旋转，最后平移；逆映射矩阵 SHALL 为 `diag(±1/scale_x, ±1/scale_y) · R(−θ)`（翻转取负号），因此任意角度下翻转与旋转的组合仍是刚体变换（行列式为 ±1/(scale_x·scale_y)），不产生剪切。纯排列快速路径与仿射路径 SHALL 旋转方向一致（正角度为屏幕顺时针）。输出帧尺寸 SHALL 恒等于输入帧尺寸。反向映射坐标落在源画布之外的像素 SHALL 填充黑色：Y 平面填 0，U/V 平面填中性灰（8 位为 128）。

执行路径按输入帧所在域分派：
- **硬件帧**：Prepare 时从图级 GPU 设备创建 `VideoKernel::kAffineRemap` pass；存在时以 `pixel_ops::ComputeAffineMapping` 得到的逆矩阵与归一化平移作为 uniform 在 GPU 上执行，输出新的硬件帧。pass 不存在或执行失败时 SHALL 回退为节点边界下载后走 CPU 路径（失败只记录一次日志）。
- **软件帧（CPU 路径）**：`Process()` SHALL 优先尝试 `pixel_ops::TryPermutePlane` 纯排列路径（rotate 为 0/90/180/270 且 scale=1.0 且 translate=0 时），命中则跳过双线性插值。未命中时调用 `pixel_ops::ComputeAffineMapping` 预计算映射常量，走 `RemapPlane`（NV12 色度用 `RemapInterleavedPlane` 以避免重复 InverseMap）。所有平面数据访问 SHALL 通过 `MediaFrame::PlaneData()`/`PlaneLinesize()` 完成。

#### Scenario: 任意角度旋转不改变输出尺寸
- **WHEN** `rotate_deg` 设为 37.5，输入帧为 1920×1080
- **THEN** 输出帧尺寸仍为 1920×1080，未被源画面覆盖的角落填充黑色

#### Scenario: 旋转叠加翻转不变形
- **WHEN** rotate_deg=30 且 flip_h=true，scale=1.0
- **THEN** 输出为镜像后旋转 30° 的矩形画面，不出现平行四边形拉伸；CPU 与 GPU 路径结果一致

#### Scenario: 纯排列场景走 TryPermutePlane
- **WHEN** 软件帧，rotate_deg=90, flip_h=false, scale=1.0, translate=0
- **THEN** TryPermutePlane 返回 true，不进入双线性插值分支

#### Scenario: NV12 色度不重复计算 InverseMap
- **WHEN** 输入软件帧格式为 NV12 且走双线性路径
- **THEN** 调用 RemapInterleavedPlane 一次，内部只执行一次仿射逆运算同时写入 U 和 V

#### Scenario: 硬件帧在 GPU 上变换
- **WHEN** 输入为 D3D11 硬件帧且 GPU pass 可用，rotate_deg=30
- **THEN** 输出为新的 D3D11 硬件帧，不发生 GPU→CPU 下载

#### Scenario: GPU 不可用时回退下载
- **WHEN** 输入为硬件帧但图中无 GPU 设备或设备不支持 pass
- **THEN** 节点下载为软件帧后按 CPU 路径处理，输出软件帧

### Requirement: ColorEffectNode 调整亮度对比度饱和度
系统 SHALL 定义 `ColorEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），对 YUV 类帧的 Y 平面做亮度/对比度线性变换，对 U/V 平面做饱和度缩放。

ColorEffectNode SHALL 提供参数（均为 `kFloat`）：`brightness`（默认 0.0）、`contrast`（默认 1.0）、`saturation`（默认 1.0）。

变换公式：
- `Y' = clamp(0, 255, (Y - 128) * contrast + 128 + brightness * 255)`
- `U' = clamp(0, 255, (U - 128) * saturation + 128)`，V 同理

`Process()` SHALL 在执行像素操作前检查恒等路径：若 brightness==0 && contrast==1.0 && saturation==1.0，直接透传。

非恒等路径按输入帧所在域分派：
- **硬件帧**：Prepare 时从图级 GPU 设备创建 `VideoKernel::kColorAdjust` pass；存在时以 `{brightness, contrast, saturation}` 为 uniform 在 GPU 上执行（公式同上，按归一化值计算），输出新的硬件帧，支持 NV12 与 P010。pass 不存在或执行失败时 SHALL 回退为节点边界下载后走 CPU 路径（失败只记录一次日志）。
- **软件帧（CPU 路径）**：SHALL 调用 `pixel_ops::BuildColorLut` 预计算 256 项 LUT，再调用 `pixel_ops::ApplyLut` 处理各平面（NV12 色度按 `ChromaPlaneLayout::row_bytes` 处理整行）。CPU 路径 SHALL 仅支持 `kYUV420P`/`kYUV422P`/`kYUV444P`/`kNV12`；不支持时记录一次 spdlog 警告并透传。所有平面数据访问 SHALL 通过 `MediaFrame::PlaneData()`/`PlaneLinesize()` 完成。

#### Scenario: LUT 路径结果与浮点一致
- **WHEN** brightness=0.2, contrast=1.0, saturation=1.0，Y 平面某像素 Y=100
- **THEN** 输出 Y 值 ≈ 151（与浮点误差 ≤1）

#### Scenario: 默认参数走恒等快速路径
- **WHEN** brightness=0, contrast=1.0, saturation=1.0
- **THEN** Process() 直接透传，不读取任何平面数据

#### Scenario: 不支持的像素格式降级为透传
- **WHEN** 输入软件帧 pixel_format 为 `kRGB32`
- **THEN** ColorEffectNode 记录一次警告日志，Process() 直接透传原始帧，不做任何像素修改，不返回错误

#### Scenario: 默认参数下为恒等变换
- **WHEN** brightness=0.0, contrast=1.0, saturation=1.0
- **THEN** 输出帧与输入帧像素值一致

#### Scenario: 硬件帧在 GPU 上调色
- **WHEN** 输入为 D3D11 硬件帧且 GPU pass 可用，saturation=0
- **THEN** 输出为新的 D3D11 硬件帧（整幅画面灰度），不发生 GPU→CPU 下载，开关该特效不改变下游帧域

#### Scenario: GPU 不可用时回退下载
- **WHEN** 输入为硬件帧但图中无 GPU 设备或设备不支持 pass
- **THEN** 节点下载为软件帧后按 CPU 路径处理，输出软件帧
