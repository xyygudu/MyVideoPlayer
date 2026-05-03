## ADDED Requirements

### Requirement: Main window layout
Qt 主窗口 SHALL 分为上下两部分：上部为视频渲染区域，下部为播放控制栏。

#### Scenario: Window displays on launch
- **WHEN** 启动 mvp_app
- **THEN** 显示主窗口，上部为黑色视频区域，下部为控制栏

### Requirement: Video display widget
视频渲染区域 SHALL 作为纯原生窗口容器（`WA_PaintOnScreen`），将 `winId()` 传递给核心库。核心库通过 SDL3 VideoRenderer 直接在该窗口上进行 GPU 加速 YUV 纹理渲染，无需 CPU 格式转换。

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
