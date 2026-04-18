## Why

项目需要一个简单的音视频播放器作为学习基础，后续可以逐步增加功能。当前没有任何播放能力，需要从零搭建基于 FFmpeg 解复用/解码 + SDL 音频输出 + Qt UI 的播放器框架，底层与 UI 分离以便复用。

## What Changes

- 新增底层播放引擎动态库（`mvp_core`），封装 FFmpeg 解复用、音视频解码、音视频同步（以音频时钟为基准）、缓存队列管理
- 新增 SDL 音频输出后端，供引擎库使用
- 新增 Qt UI 应用（`mvp_app`），上部视频渲染区域 + 底部控制栏（播放/暂停、Seek 进度条、时长显示）
- 新增 CMake 构建系统，支持 VS Code 调试及生成 VS2026 工程
- 遵循 Google C++ Style，跨平台设计（Windows/Linux/macOS）

## Capabilities

### New Capabilities
- `demux-decode`: FFmpeg 解复用与音视频解码管线，包含 packet/frame 缓存队列
- `av-sync`: 以音频时钟为基准的音视频同步机制
- `playback-control`: 播放、暂停、Seek 控制接口
- `player-ui`: Qt 界面——视频渲染区 + 播放控制栏

### Modified Capabilities

（无已有能力需要修改）

## Impact

- **新增依赖**: FFmpeg 7.1.1（解复用/解码）、SDL3（音频输出）、Qt 6.7.3（UI）
- **构建系统**: 新建 CMakeLists.txt，需配置 FFmpeg/SDL/Qt 的 find_package 路径
- **输出产物**: `mvp_core` 动态库 + `mvp_app` 可执行文件
- **平台**: 首先在 Windows (MSVC) 上开发验证，架构保持跨平台兼容
