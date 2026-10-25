## Purpose

Defines the `mvp::pixel_ops` utility namespace — pure free functions for
pixel-plane transformations (LUT, affine remapping, bilinear sampling,
permutation copy). Operates on raw `uint8_t*` + stride. Zero dependency on
AVFrame, MediaFrame, MediaBuffer, or any graph-layer types.
## Requirements
### Requirement: pixel_ops 像素变换纯函数工具集
系统 SHALL 定义 `mvp::pixel_ops` 命名空间，提供零依赖的像素变换原语。所有函数接受 `uint8_t*` + stride 作为数据入口，不接受 `AVFrame*`/`MediaFrame`/`MediaBuffer` 等包装类型。

`pixel_ops` SHALL 提供以下函数：
- `BuildColorLut(brightness, contrast, saturation) -> ColorLut`：根据颜色调节参数预计算 Y 平面和 UV 平面各自的 256 项查找表
- `ApplyLut(plane, linesize, width, height, lut)`：对单平面逐像素套用 LUT，原地修改
- `ComputeAffineMapping(params, plane_width, plane_height) -> AffineMapping`：根据用户变换参数和目标平面尺寸预计算仿射逆映射常量
- `RemapPlane(src, dst, width, height, comp_stride, comp_offset, fill, mapping)`：对单平面逐像素做仿射反向映射 + 双线性采样 + 填充
- `RemapInterleavedPlane(src, dst, width, height, fill, mapping)`：对 NV12 交织色度平面做一次逆映射同时写入 U 和 V
- `TryPermutePlane(src, dst, width, height, comp_stride, comp_offset, params) -> bool`：若变换为纯排列重排（旋转 0/90/180/270° + 任意翻转 + scale=1.0 + translate=0），用整数下标重排完成拷贝并返回 true；否则返回 false

#### Scenario: ApplyLut 对单平面生效
- **WHEN** 调用 `ApplyLut(data, linesize, 1920, 1080, lut)` 且 `lut` 为恒等表（`lut[i]==i`）
- **THEN** 平面像素值不变

#### Scenario: RemapPlane 越过源边界时填充指定值
- **WHEN** 逆映射坐标落在源平面之外
- **THEN** 对应目标像素填 `fill` 值

#### Scenario: TryPermutePlane 命中纯排列场景
- **WHEN** 参数为 rotate_deg=90, flip_h=true, scale=1.0, translate=0
- **THEN** TryPermutePlane 返回 true，目标平面为正确排列，不执行浮点运算

### Requirement: pixel_ops 不依赖任何 graph 或帧封装类型
`pixel_ops.h` SHALL 仅依赖 `<cstdint>`。

#### Scenario: pixel_ops.h 可被独立编译
- **WHEN** 在仅包含 `<cstdint>` 的环境中编译 `pixel_ops.h`
- **THEN** 编译通过

### Requirement: 仿射映射以像素中心为坐标
`ComputeAffineMapping` SHALL 以整数坐标表示像素中心，n 个像素的平面中心取 `(n − 1) / 2`，使水平镜像精确映射 `x → n − 1 − x`。GPU 仿射着色器 SHALL 使用同一约定，保证 CPU 与 GPU 路径输出一致。

#### Scenario: 镜像不产生黑边与错位
- **WHEN** 对 640 宽的平面做水平翻转
- **THEN** 输出第 0 列等于输入第 639 列，不出现越界采样的黑列

### Requirement: 置换路径只处理精确的四分之一圈
`TryPermutePlane` SHALL 只在缩放为 1、平移为 0、旋转角为 90° 的整数倍（误差 < 1e-3°）时走无插值的像素置换；其他角度 SHALL 返回 false 交给双线性重采样。交错色度平面（NV12）的两个分量 SHALL 分别按分量偏移置换。

#### Scenario: 小角度旋转不被吞掉
- **WHEN** 只设置 `rotate_deg = 20`
- **THEN** 置换路径拒绝处理，双线性重采样产生旋转后的画面，与 GPU 路径一致

#### Scenario: NV12 翻转保留两个色度分量
- **WHEN** 对 NV12 帧做水平翻转
- **THEN** U 与 V 分量都被镜像，颜色不变

