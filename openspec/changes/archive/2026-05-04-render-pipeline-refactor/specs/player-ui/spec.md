## MODIFIED Requirements

### Requirement: Video display widget
视频渲染区域 SHALL 作为 core 层 `VideoRenderer`（SDL3 GPU）的宿主容器。Qt VideoWidget SHALL 将自身的 native window handle 传递给 core 层的 VideoRenderer，由 VideoRenderer 在该区域内进行 GPU 渲染。VideoWidget 自身 SHALL 不参与任何像素数据处理。

#### Scenario: Display video frames during playback
- **WHEN** core 层 VideoRenderer 渲染新的视频帧
- **THEN** 视频区域更新显示该帧，画面随视频内容变化

#### Scenario: Maintain aspect ratio
- **WHEN** 窗口大小改变
- **THEN** 视频画面保持原始宽高比，居中显示，空余部分填黑（由 VideoRenderer 处理）

#### Scenario: Widget resize propagates to renderer
- **WHEN** Qt VideoWidget 尺寸变化
- **THEN** 通知 core 层 VideoRenderer 更新渲染区域大小
