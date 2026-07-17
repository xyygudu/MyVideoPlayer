## Why

`TransformEffectNode` 和 `ColorEffectNode` 的纯标量像素循环在 1080p 下耗时严重（transform ~47ms/帧，color ~6ms/帧），远达不到 30fps 的帧预算。同时两个节点内部直接操作 `AVFrame*` 调用 `av_frame_make_writable`/`av_frame_get_buffer` 等 FFmpeg C API，违反了"第三方库类型不出现在公共头"的约束，且引用计数管理容易出错。

## What Changes

- **新增 `pixel_ops.h/cc`**（`mvp::pixel_ops` 命名空间）：将像素变换原语抽成纯函数（接受 `uint8_t*` + stride，不依赖 `AVFrame`/`MediaFrame`/任何 graph 类型），对标 FFmpeg `libavutil` 的自由函数模式。
- **ColorEffectNode 用 LUT 替代逐像素浮点运算**：预计算 256 项 `uint8_t` 查找表，像素循环变成一次数组下标，预计 ~5x 加速。
- **TransformEffectNode 三处优化**：
  - 恒等/纯排列变换快速路径（0°/90°/180°/270° + 翻转，无插值场景走整数下标重排，接近 memcpy 速度）
  - `InverseMap` 帧级常量提升（cx/cy/tx/ty 从每像素计算改为每平面一次）
  - NV12 色度去重（U/V 共用一次仿射逆运算）
  - 缓存输出 AVFrame，每帧不再堆分配
- **`MediaFrame` 封装增强**：新增 `PlaneData()`/`PlaneLinesize()`/`width()`/`height()`/`format()`/`MakeWritable()`/`CreateSameFormat()` 方法，使 effect node 不再需要调用 `RawFrame()` 和任何 `av_frame_*` 函数。
- 两个 effect node 的 `Process()` 改为通过 `MediaFrame` 公共方法操作数据，移除对 `AVFrame*` 的直接依赖。

## Capabilities

### New Capabilities
- `pixel-ops`：像素变换工具函数集（LUT、仿射映射、双线性采样、平面重映射），纯数据操作，零依赖

### Modified Capabilities
- `graph-effect-nodes`：ColorEffectNode/TransformEffectNode 的性能优化路径 + MediaFrame 接口约束
- `frame-abstraction`：`MediaFrame` 新增平面数据访问和可写性管理方法（**BREAKING**：`RawFrame()` 降级为内部实现细节，外部应通过 PlaneData() 等新方法访问数据）

## Impact

- 新增文件：`src/media/pixel_ops.h/cc`、可能新增 `src/media/pixel_ops_test.cc`（如果有测试框架）
- 修改文件：`src/media/media_frame.h/cc`（新增方法）、`src/media/nodes/color_effect_node.cc`（LUT 路径）、`src/media/nodes/transform_effect_node.cc`（快速路径 + 帧常量提升 + NV12 去重 + 缓存输出帧）、`src/media/nodes/transform_effect_node.h`（新增缓存帧成员）、`src/media/ffmpeg_utils.h`（`ChromaPlaneLayout` 等共享结构体可能迁入 pixel_ops）
- 不改变 `IEffectNode`/`EffectManager`/`MediaPlayer`/`EffectPanel` 的公共接口
- 不引入新的第三方依赖
- `MediaFrame::RawFrame()` 不删除（向后兼容），但在 effect node 内部不再使用
