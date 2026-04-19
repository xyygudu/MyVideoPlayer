## Context

MyVideoPlayer 项目当前没有任何日志设施。调试依赖断点和偶尔的 printf，运行时无可观测性。spdlog 是 C++ 社区最主流的高性能日志库，已预编译安装在 `D:\Install\spdlog`（含 `spdlogConfig.cmake`），可通过 `find_package(spdlog)` 直接集成。

现有代码结构：
- `src/core/` — `mvp_core` 动态库（Demuxer / Decoder / AudioOutput / Clock / Player / PacketQueue / FrameQueue）
- `src/app/` — `mvp_app` Qt 可执行文件（MainWindow / VideoWidget）
- 公开头文件在 `src/core/include/mvp/`

## Goals / Non-Goals

**Goals:**
- 引入 spdlog，在 mvp_core 和 mvp_app 中可用
- 提供统一的日志初始化接口 `mvp::logging::Init()`，默认控制台 sink
- 支持可选的文件 sink（`mvp::logging::EnableFileLogging(path)`）
- 在所有关键操作处输出结构化日志（级别：info / warn / error）
- CMakePresets.json 的 default 和 vs2026 两个 preset 都正确引入 spdlog

**Non-Goals:**
- 不做异步日志（当前规模不需要）
- 不做日志轮转配置（后续再加）
- 不对外暴露 spdlog 头文件（`mvp/logging.h` 仅暴露初始化 API，实现细节不泄露）

## Decisions

### 1. spdlog 作为 PRIVATE 依赖链接到 mvp_core

**理由**: mvp_core 是动态库，公开头文件不应暴露 spdlog 类型。`logging.h` 只声明 `Init()` 和 `EnableFileLogging()` 等纯 C++ 接口，实现文件内部 `#include <spdlog/spdlog.h>`。mvp_app 也单独 PRIVATE 链接 spdlog，以便 app 层代码直接用 `SPDLOG_INFO` 等宏。

**备选**: 仅 mvp_core 链接，app 通过 core 间接使用 → 放弃，因为 app 层也需要在 UI 事件处打日志。

### 2. 默认控制台 sink，文件 sink 可选启用

**理由**: 开发阶段控制台输出最直观；生产使用时可调用 `EnableFileLogging` 添加文件 sink。两个 sink 共享同一个 logger 实例。

### 3. 使用 spdlog 预编译静态库（spdlogd.lib）

**理由**: 本机已编译安装，比 header-only 模式编译更快。通过 `find_package(spdlog REQUIRED)` + `spdlog::spdlog` target 引入。

### 4. 日志格式

采用格式：`[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v`
- 时间戳精确到毫秒
- 日志级别
- 源文件:行号（便于定位）
- 消息内容

### 5. 日志插入位置策略

| 模块 | 日志点 | 级别 |
|------|--------|------|
| Demuxer | Open 成功/失败、Close、Seek 请求、流信息 | info / error |
| Decoder | Open 成功/失败、解码错误 | info / warn / error |
| AudioOutput | Open 成功/失败、SDL 错误 | info / error |
| PacketQueue / FrameQueue | Abort 调用、Flush | debug / info |
| Player | Open / Close / Play / Pause / Seek、线程启停 | info |
| MainWindow | 用户操作（打开文件、播放、暂停、Seek） | info |

## Risks / Trade-offs

- **[Risk] 机器特定路径** → CMakePresets.json 中增加 spdlog_DIR 变量，与其他库路径一致管理；其他开发者需修改
- **[Risk] 日志性能开销** → spdlog 同步模式在当前调用频率下开销可忽略；未来高频场景可切异步模式
- **[Risk] DLL 边界的全局 logger** → mvp_core 和 mvp_app 各自持有 spdlog 静态库副本，logger 实例在 mvp_core 中管理，app 层通过 `spdlog::get("mvp")` 获取同名 logger 或各自创建 → 因为是 PRIVATE 链接静态库，两侧各有独立的 spdlog 全局注册表。简化方案：core 和 app 各自初始化自己的 default logger，名称不同即可（`mvp_core` / `mvp_app`），日志目的地统一由同一初始化函数配置
