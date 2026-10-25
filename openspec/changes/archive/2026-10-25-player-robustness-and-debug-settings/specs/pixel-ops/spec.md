## ADDED Requirements

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
