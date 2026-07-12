## ADDED Requirements

### Requirement: Effects tab 按特效分组展示参数，控件类型跟随参数类型
右侧 `EffectPanel` 的 "Effects" tab SHALL 为每个已注册的特效（当前为 Transform 与 Color 两个，来自 `MediaPlayer::EffectInfos()` 返回的 `EffectInfo`）渲染一个独立分组，分组标题使用 `EffectInfo::display_name`，分组标题旁 SHALL 附带一个启用勾选框绑定 `EffectInfo::enabled`。分组内按 `EffectInfo::params` 的顺序为每个 `EffectParam` 生成对应控件：`kFloat` 用 `QSlider`（连续）+ `QLabel` 数值显示，`kInt` 用步进式 `QSlider`（`singleStep=1`）或 `QSpinBox`，`kBool` 用 `QCheckBox`，`kEnum` 用 `QComboBox`（选项文案取自 `enum_labels`）。

#### Scenario: 打开视频后面板显示两个分组
- **WHEN** 用户打开一个视频文件，`MediaPlayer::Open()` 成功
- **THEN** EffectPanel 显示 "Transform" 和 "Color" 两个分组，每组内控件数量与对应 `EffectInfo::params` 数量一致，每组标题旁有一个启用勾选框

#### Scenario: 控件类型由参数类型决定
- **WHEN** 面板渲染 Transform 分组中 `flip_h`（`kBool`）与 `rotate_deg`（`kFloat`）两个参数
- **THEN** `flip_h` 渲染为 `QCheckBox`，`rotate_deg` 渲染为连续 `QSlider`

#### Scenario: 控件初始值来自参数默认值
- **WHEN** 面板首次渲染某个参数控件
- **THEN** 控件初始状态对应 `EffectParam::default_value`

### Requirement: 拖动/切换控件实时调节参数与启用状态
用户调整某个参数控件时，SHALL 立即调用 `MediaPlayer::SetEffectParam(effect_id, param_id, value)`，无需等待松开鼠标或点击确认按钮；切换分组标题旁的启用勾选框时，SHALL 立即调用 `MediaPlayer::SetEffectEnabled(effect_id, enabled)`。

#### Scenario: 拖动亮度滑块
- **WHEN** 用户拖动 "Color" 分组下的 "亮度" 滑块到新位置
- **THEN** 面板立即调用 `SetEffectParam("color", "brightness", <新值>)`，后续渲染帧体现新的亮度效果

#### Scenario: 取消勾选禁用某特效
- **WHEN** 用户取消勾选 "Transform" 分组标题旁的启用勾选框
- **THEN** 面板立即调用 `SetEffectEnabled("transform", false)`，后续渲染帧不再体现几何变换效果，该分组下的参数控件 SHALL 置灰但仍显示当前值

### Requirement: 面板在关闭/重新打开源时重置
当 `MediaPlayer::Close()` 或重新 `Open()` 新文件后，EffectPanel SHALL 重新拉取 `EffectInfos()` 并将所有控件重置为返回结果中的 `default_value`/启用状态，不保留上一个源的参数状态。

#### Scenario: 打开新文件后面板重置为默认值
- **WHEN** 用户在调整过特效参数后，通过菜单打开另一个视频文件
- **THEN** EffectPanel 中所有控件恢复到各自的默认值和启用状态

### Requirement: 右侧面板由 TabWidget 承载，当前仅含 Effects tab
`EffectPanel` SHALL 以 `QTabWidget` 作为顶层容器，当前仅注册一个名为 "Effects" 的 tab；后续新增 Info/Playlist 等 tab 时 SHALL 能够以增量方式追加，不需要改动 Effects tab 内部实现。

#### Scenario: 面板仅展示一个 tab
- **WHEN** 主窗口启动
- **THEN** 右侧面板的 QTabWidget 只包含一个 "Effects" tab 页
