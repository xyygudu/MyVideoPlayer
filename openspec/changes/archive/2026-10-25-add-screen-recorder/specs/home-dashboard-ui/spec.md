## ADDED Requirements

### Requirement: 主页提供录屏卡片
HomePage SHALL 在播放器、转码器卡片之后提供「录屏」卡片：缩略图为代码绘制的强调色显示器与红色录制圆点（不依赖图片资源），描述为「录制全屏、窗口或选定区域，可同时录制系统声音、麦克风与摄像头画面。」点击 SHALL 发出 `NavigateRequested(Page::kRecorder)`。

#### Scenario: 点击录屏卡片
- **WHEN** 在主页点击「录屏」卡片
- **THEN** 切换到录屏页，导航栏「录屏」项变为选中态
