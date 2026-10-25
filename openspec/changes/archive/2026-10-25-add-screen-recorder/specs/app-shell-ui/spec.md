## ADDED Requirements

### Requirement: 录屏导航项位于快速访问组
NavigationBar 的「快速访问」组 SHALL 在「转码器」之后提供「录屏」项（`recorder.svg` 图标：圆角屏幕中央一个实心圆点），对应 `NavigationBar::Page::kRecorder`；MainWindow SHALL 将其路由到常驻的 `RecorderPage`，标题栏显示「录屏」。录屏页请求播放录像时，MainWindow SHALL 切换到播放器页并打开该文件。

#### Scenario: 打开录屏页
- **WHEN** 点击导航栏「录屏」
- **THEN** 内容区切换到录屏页，标题栏显示「录屏」，只有「录屏」项处于选中态

#### Scenario: 从录屏页播放
- **WHEN** 在录屏页的最近录制中双击一项
- **THEN** 导航切换到「播放器」并播放该文件
