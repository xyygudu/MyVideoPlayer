# SVG 图标设计规范

> 适用于 MyVideoPlayer 项目中的所有功能图标。2026-07-26 定稿。

## 文件位置

所有图标：`src/app/resources/icons/`
资源列表：`src/app/resources/icons.qrc`
运行时路径：`:/icons/<文件名>.svg`

## viewBox 约定

**一律使用 `viewBox="10 10 80 80"`**

即在 `0 0 100 100` 的基础上统一裁剪 10px 边距，等效 1.25× 放大。
如果某个图标的元素坐标超出了 10~90 范围，则可适当放宽（如 `5 5 90 90`）。

> 原因：Logo 设计留白 40-50%，但功能图标需要更紧凑。
> 80×80 配合 10px 边距，在所有图标形状间保持一致的呼吸空间。

## 坐标与描边规则

| 规则 | 值 | 说明 |
|------|-----|------|
| 内容跨度 | **50-65%** of viewBox | 在 80×80 中至少跨越 40-52px |
| 描边宽度（统一） | **7** | 所有描边类元素统一，无例外 |
| 填充形状描边 | **4** | 仅用于填充形状外围防锯齿缝隙，不增加视觉宽度 |
| 圆角连接 | `stroke-linejoin="round"` | 所有路径拐角 |
| 圆角端点 | `stroke-linecap="round"` | 所有路径端点 |
| 矩形圆角 | `rx` 最小 3px | 始终使用圆角矩形 |

**填充形状**（如播放三角）：同时使用 `fill="currentColor"` 和 `stroke="currentColor"`。
描边可以防止小尺寸下抗锯齿产生的缝隙。

## 颜色约定

| 用途 | SVG 颜色写法 | 加载方式 |
|------|-------------|---------|
| QIcon / NavItem / IconButton | `currentColor` | C++ 代码替换为 `kTextPrimary` 或 `kAccent` |
| QSpinBox / 样式表 `image:` | `#1F2329`（显式） | 样式表无法解析 `currentColor` |

样式表专用图标文件命名：`spin_<名称>.svg`（如 `spin_up.svg`）。

## 渲染路径速查

| 用途 | C++ 函数 | 输出 | 颜色转换 |
|------|---------|------|---------|
| 导航栏图标 | `CreateNavIcon(path)` | QIcon 18×18 | currentColor → kTextPrimary |
| 首页卡片缩略图 | `LoadAccentSvg(path, 48)` | QPixmap 48×48 | currentColor → kAccent |
| IconButton 自定义 | `LoadButtonIcon(path, size)` | QIcon ~16×16 | currentColor → kTextPrimary |
| QSpinBox 样式表 | `image: url(...)` in QSS | 10×10 | `#1F2329` 固化 |

## 新增图标步骤

1. 按规范创建 `src/app/resources/icons/<name>.svg`
2. 将 `<file>icons/<name>.svg</file>` 追加到 `src/app/resources/icons.qrc`
3. 触碰 `.qrc` 时间戳或删除 `build/src/app/qrc_icons.cpp` 强制 rcc 重新编译
4. 在 C++ 代码中用合适的辅助函数加载
5. 如果用于 QSpinBox/QComboBox，额外创建 `spin_<name>.svg`（显式颜色）
