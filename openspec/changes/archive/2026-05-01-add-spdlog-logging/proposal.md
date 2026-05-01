## Why

播放器项目目前没有任何日志输出，调试和问题排查完全依赖断点和 printf。需要引入结构化日志框架，在关键操作（文件打开/关闭、解码、音视频同步、错误）处输出日志，提升开发和运维效率。spdlog 已安装在本机 `D:\Install\spdlog`，具备原生 CMake 支持，集成成本低。

## What Changes

- 在 CMake 构建系统中引入 spdlog 依赖（顶层 CMakePresets.json 配置路径，core 和 app 的 CMakeLists.txt 链接 spdlog）
- 新增日志初始化模块：默认使用控制台 sink，可通过 API 切换为文件 sink（或同时输出）
- 在 mvp_core 关键位置插入日志：Demuxer 打开/关闭、Decoder 初始化/错误、AudioOutput 打开/回调异常、PacketQueue/FrameQueue 溢出/Abort、Player 生命周期与 Seek 操作
- 在 mvp_app 关键位置插入日志：MainWindow 用户操作（打开文件、播放/暂停/Seek）

## Capabilities

### New Capabilities
- `logging`: 日志基础设施——初始化、sink 管理（控制台/文件）、在核心库与 UI 层关键路径输出结构化日志

### Modified Capabilities

（无需修改现有规格——日志是横切关注点，不改变已有功能的行为契约）

## Impact

- **依赖**: 新增 spdlog 第三方库（静态库 spdlogd.lib，header-only 可选但本项目用预编译版本）
- **构建**: CMakePresets.json 增加 spdlog 路径；src/core 和 src/app 的 CMakeLists.txt 增加 find_package + target_link_libraries
- **代码**: 所有 .cc 文件中在关键操作处新增 SPDLOG_INFO / SPDLOG_WARN / SPDLOG_ERROR 调用
- **运行时**: 默认控制台输出；可选文件日志写入当前工作目录
- **API**: mvp_core 新增公开头文件 `mvp/logging.h`，暴露日志初始化接口
