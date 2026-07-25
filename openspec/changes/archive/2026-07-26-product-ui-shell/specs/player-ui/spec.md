## MODIFIED Requirements

### Requirement: Video display widget
VideoWidget SHALL no longer hardcode WA_PaintOnScreen in its constructor.
Instead it SHALL toggle the attribute at runtime: off by default (Qt fills
the surface, preventing stale page content from leaking through
QStackedWidget), on via SetVideoPlaying(true) when SDL starts rendering.

#### Scenario: No video — Qt-managed surface
- **WHEN** 未打开任何视频文件
- **THEN** WA_PaintOnScreen 为 false，Qt 正常管理 VideoWidget 的表面

#### Scenario: Video playing — SDL-managed surface
- **WHEN** player_->Open() 成功后调用 video_widget_->SetVideoPlaying(true)
- **THEN** WA_PaintOnScreen 为 true，Qt 停止绘画，SDL 独占渲染

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
