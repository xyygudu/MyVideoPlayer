## MODIFIED Requirements

### Requirement: Main window layout
Qt 主窗口 SHALL 使用 `QSplitter(Qt::Horizontal)` 分为左右两部分：左侧为原有的视频渲染区域 + 播放控制栏（`QVBoxLayout` 容器，布局不变），右侧为新增的 `EffectPanel`（`QTabWidget` 承载，详见 effect-panel-ui 能力）。

#### Scenario: 窗口显示时分为左右两栏
- **WHEN** 启动 mvp_app
- **THEN** 显示主窗口，左侧为视频区域+控制栏，右侧为 EffectPanel，两者可通过 QSplitter 拖拽调整宽度比例

#### Scenario: 左侧内部布局保持不变
- **WHEN** 观察左侧容器内部
- **THEN** 视频渲染区域在上、播放控制栏在下，与本次改动前一致

## ADDED Requirements

### Requirement: 右侧面板宿主由 MainWindow 持有
`MainWindow` SHALL 持有一个 `EffectPanel*` 成员，构造时创建并加入 `QSplitter` 右侧；`EffectPanel` 内部逻辑（tab 结构、参数分组、滑块绑定）由 effect-panel-ui 能力定义，`MainWindow` 仅负责生命周期持有与布局挂载。

#### Scenario: 打开文件后面板与播放器联动
- **WHEN** `MainWindow::OnOpenFile` 成功调用 `player_->Open()`
- **THEN** `MainWindow` 通知 `EffectPanel` 刷新（调用其重新拉取 `EffectInfos()` 的接口）
