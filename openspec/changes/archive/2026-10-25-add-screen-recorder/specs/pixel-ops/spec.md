## ADDED Requirements

### Requirement: pixel_ops 提供 BGRA 绘制原语
`pixel_ops` SHALL 提供对 `BgraImage{data, width, height, stride}` 视图的绘制函数，全部按目标边界裁剪：
- `BlitBgra(src, src_stride, width, height, dst, x, y, mirror)`：把 BGRA 图像贴到 (x, y)，`mirror` 时每行水平翻转；
- `OutlineRectBgra(dst, x, y, width, height, thickness, rgb)`：在矩形内侧绘制 `thickness` 像素宽的不透明边框；
- `BlendDiscBgra(dst, cx, cy, radius, rgb, alpha)`：以 `alpha` 不透明度混合圆盘，边缘一像素抗锯齿。

`pixel_ops.h` SHALL 仍只依赖 `<cstdint>`。

#### Scenario: 负坐标贴图
- **WHEN** 把 3x3 图像贴到 (-1, -1)
- **THEN** 目标 (0,0) 是源 (1,1)，源第一行第一列被裁掉

#### Scenario: 圆盘边缘
- **WHEN** 半径 5、不透明度 1 的红色圆盘混合到黑色画布
- **THEN** 圆心为纯红，距圆心正好 5 像素处红色约为 128
