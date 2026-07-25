## ADDED Requirements

### Requirement: Main window layout
`PlayerPage`（原为 `MainWindow` 直接持有）SHALL 使用 `QSplitter(Qt::Horizontal)` 分为左右两部分：左侧为原有的视频渲染区域 + 播放控制栏（`QVBoxLayout` 容器，布局不变），右侧为 `EffectPanel`（`QTabWidget` 承载，详见 effect-panel-ui 能力）。`PlayerPage` 作为一个独立页面被 `app-shell-ui` 的 `QStackedWidget` 持有，不再是应用的顶层中心控件；播放行为本身不变，仅宿主容器从 `MainWindow` 变为 `PlayerPage`。

#### Scenario: 页面显示时分为左右两栏
- **WHEN** 导航到"播放器"页面
- **THEN** `PlayerPage` 显示，左侧为视频区域+控制栏，右侧为 EffectPanel，两者可通过 QSplitter 拖拽调整宽度比例

#### Scenario: 左侧内部布局保持不变
- **WHEN** 观察左侧容器内部
- **THEN** 视频渲染区域在上、播放控制栏在下，与本次改动前一致

#### Scenario: 控制栏按钮改为自定义图标按钮
- **WHEN** 观察播放/暂停、打开文件按钮
- **THEN** 使用 `IconButton` 自定义绘制的图标（而非纯文本/Unicode 字符），点击行为与此前一致

### Requirement: 右侧面板宿主由 PlayerPage 持有
`PlayerPage` SHALL 持有一个 `EffectPanel*` 成员，构造时创建并加入 `QSplitter` 右侧；`EffectPanel` 内部逻辑（tab 结构、参数分组、滑块绑定）由 effect-panel-ui 能力定义，`PlayerPage` 仅负责生命周期持有与布局挂载。`PlayerPage` 同样持有 `mvp::MediaPlayer` 实例（原由 `MainWindow` 持有），`MainWindow` 不再直接引用 `MediaPlayer`。

#### Scenario: 打开文件后面板与播放器联动
- **WHEN** `PlayerPage::OnOpenFile` 成功调用 `player_->Open()`
- **THEN** `PlayerPage` 通知 `EffectPanel` 刷新（调用其重新拉取 `EffectInfos()` 的接口）

#### Scenario: 页面常驻，播放状态在导航切换后保留
- **WHEN** 用户在播放器页面播放视频后导航到主页，再导航回播放器
- **THEN** 播放继续进行（未暂停/未重建），因为 `PlayerPage` 是 `QStackedWidget` 中的常驻页面，未被销毁

### Requirement: Video display widget
视频渲染区域 SHALL 作为原生窗口容器（`WA_NativeWindow`），将 `winId()` 传递给核心
库。核心库通过 SDL3 VideoRenderer 直接在该窗口上进行 GPU 加速 YUV 纹理渲染。

SDL 渲染期间 SHALL 启用 `WA_PaintOnScreen` 以防止 Qt 在 SDL 画面上叠加像素；
无视频渲染时 SHALL 关闭 `WA_PaintOnScreen`，由 Qt 管理表面（防止 QStackedWidget
页面切换时残留上一页内容）。此开关由 PlayerPage 在打开/关闭文件时通过
VideoWidget::SetVideoPlaying() 控制。

#### Scenario: Display video frames during playback
- **WHEN** 核心库 VideoRenderLoop 获取到新的视频帧
- **THEN** SDL3 renderer 将 YUV 纹理上传到 GPU 并渲染到 VideoWidget 的原生窗口

#### Scenario: Maintain aspect ratio
- **WHEN** 窗口大小改变
- **THEN** VideoWidget 通过 resizeEvent 通知核心库，VideoRenderer 保持原始宽高比居中显示，空余部分填黑

### Requirement: Play/Pause button
控制栏 SHALL 包含播放/暂停按钮，切换播放状态。

#### Scenario: Click play when paused
- **WHEN** 当前暂停状态，点击播放按钮
- **THEN** 调用核心库 `Play()`，按钮图标变为暂停图标

#### Scenario: Click pause when playing
- **WHEN** 当前播放状态，点击暂停按钮
- **THEN** 调用核心库 `Pause()`，按钮图标变为播放图标

### Requirement: Progress slider
控制栏 SHALL 包含进度条（QSlider），显示当前播放进度，可拖拽 Seek。

#### Scenario: Slider tracks playback progress
- **WHEN** 正在播放
- **THEN** 进度条滑块位置随 `CurrentPosition()` 实时更新

#### Scenario: User drags slider to seek
- **WHEN** 用户拖拽进度条滑块到新位置并释放
- **THEN** 调用核心库 `Seek()` 跳转到对应时间

### Requirement: Time display
控制栏 SHALL 显示当前播放时间和总时长，格式为 `HH:MM:SS / HH:MM:SS`。

#### Scenario: Time label updates during playback
- **WHEN** 正在播放
- **THEN** 当前时间标签实时更新，总时长显示不变

### Requirement: Open file action
应用 SHALL 提供打开文件的方式（菜单或按钮），弹出文件选择对话框，选择视频文件后开始播放。

#### Scenario: Open file and start playback
- **WHEN** 用户通过文件对话框选择一个视频文件
- **THEN** 调用核心库 `Open()` + `Play()`，视频开始播放
