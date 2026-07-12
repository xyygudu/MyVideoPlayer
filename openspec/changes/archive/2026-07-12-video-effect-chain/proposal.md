## Why

播放器目前的管线是固定的 `Demux -> Decoder -> Sink`，无法在播放中对画面做任何后处理。用户希望在解码后、渲染前插入一条可配置的视频特效链（先支持几何变换和颜色调节两类），并通过 UI 右侧新增的 Effects 面板实时查看和调节每个特效的参数。这是学习"播放器管线如何扩展中间处理阶段"的一个具体练习，需要在不破坏现有音视频同步机制、不引入新线程模型的前提下完成。

## What Changes

- 新增 `IEffectNode` 抽象（`NodeType::kTransform`，默认 `ThreadingMode::kPassive`），作为所有可调参特效节点的公共接口：暴露有序的类型化参数列表（`EffectParam`，携带 float/int/bool/enum 类型标签、当前值、默认值、范围）+ 线程安全的 get/set，以及启用/禁用开关，供上层 UI 反射式绑定。
- 新增 `TransformEffectNode`：合并任意角度旋转、翻转（水平/垂直）、缩放、平移为一次像素重映射（反向仿射变换 + 双线性插值），画布尺寸恒定不变，避免多趟遍历。
- 新增 `ColorEffectNode`：亮度、对比度、饱和度的逐像素调整（Y 平面线性变换 + UV 平面围绕中心缩放）。
- 两者均为纯 CPU 手写实现（不依赖 libavfilter），参数用原子变量存储，UI 线程写、Process() 所在线程读，拖动滑块无需重建任何图或走 command 下发即可实时生效。
- 新增 `EffectManager`（`MediaPlayer::Impl` 的成员，非拥有地索引已注册的 `IEffectNode`），替代裸指针 map，统一承担特效查询/参数下发/启用禁用的路由职责。
- `MediaPlayer::BuildGraph` 在 video 分支的 `DecoderNode -> VideoSinkNode` 之间按顺序插入 `TransformEffectNode -> ColorEffectNode`（默认 Passive，与 Decoder 同线程；VideoSinkNode 的输入 Link 依旧是唯一的同步用队列，不新增队列）。
- `MediaPlayer` 新增查询特效信息列表（`EffectInfo`）+ 按参数 id 设置数值 + 启用/禁用特效的接口，替换现有未实现的 `SetFilter` 占位方法（**BREAKING**：移除 `MediaPlayer::SetFilter`）。
- Qt 主窗口布局从上下两段改为左右分栏：左侧为现有播放画布+控制栏，右侧新增 `QTabWidget`，当前只包含一个 "Effects" tab，逐个特效分组展示启用开关与参数控件（控件类型随参数类型变化），实时调用 MediaPlayer 的特效接口。

## Capabilities

### New Capabilities
- `graph-effect-nodes`: `IEffectNode` 接口、`TransformEffectNode`、`ColorEffectNode` 的行为、参数模型与线程安全约定。
- `effect-panel-ui`: 右侧 Effects 面板的布局、参数展示与实时调节交互。

### Modified Capabilities
- `playback-control`: `MediaPlayer` 新增特效参数查询/设置接口，移除未实现的 `SetFilter` 占位方法。
- `playback-graph-builder`: `BuildGraph` 在视频分支中插入 `TransformEffectNode`/`ColorEffectNode` 两个节点。
- `player-ui`: 主窗口布局由上下两段改为左右分栏，新增右侧 `QTabWidget` 容器（当前仅含 Effects tab）。

## Impact

- 受影响代码：`src/media/nodes/`（新增两个 effect node 文件）、`src/media/media_player.cc/.h`（BuildGraph 接线 + 新增公共 API，移除 `SetFilter`）、`src/app/main_window.cc/.h`（布局重构）、新增 `src/app/effect_panel.cc/.h`（右侧面板 widget）。
- 不涉及新的第三方依赖（不引入 libavfilter 使用面）。
- 不改变现有 AV 同步逻辑（`VideoSinkNode` 的 frame_timer / 丢帧保护不变），效果链只影响 Decoder 与 Sink 之间的数据内容，不影响时钟或队列结构。
- `MediaPlayer::SetFilter` 是 **BREAKING** 移除（此前从未实现，无外部调用方）。
