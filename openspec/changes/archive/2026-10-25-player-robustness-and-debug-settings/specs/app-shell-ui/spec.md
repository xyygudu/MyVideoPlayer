## ADDED Requirements

### Requirement: 设置导航项固定在导航栏底部
NavigationBar SHALL 在伸缩空白之后、导航栏底部提供「设置」项（`settings.svg` 图标），对应 `NavigationBar::Page::kSettings`；MainWindow SHALL 将其路由到常驻的 `SettingsPage`，标题栏显示「设置」。选中态 SHALL 与其他导航项互斥。

#### Scenario: 打开设置页
- **WHEN** 点击导航栏底部的「设置」
- **THEN** 内容区切换到设置页，标题栏显示「设置」，只有「设置」项处于选中态
