## 1. 构建系统集成 spdlog

- [x] 1.1 CMakePresets.json：在 default 和 vs2026 两个 preset 的 cacheVariables 中添加 `spdlog_DIR` 指向 `D:/Install/spdlog/lib/cmake/spdlog`
- [x] 1.2 src/core/CMakeLists.txt：添加 `find_package(spdlog REQUIRED)` 并在 target_link_libraries 中 PRIVATE 链接 `spdlog::spdlog`
- [x] 1.3 src/app/CMakeLists.txt：添加 `find_package(spdlog REQUIRED)` 并在 target_link_libraries 中 PRIVATE 链接 `spdlog::spdlog`
- [x] 1.4 验证 `cmake --preset default && cmake --build build` 构建成功

## 2. 日志初始化模块

- [x] 2.1 新建 `src/core/include/mvp/logging.h`：声明 `mvp::logging::Init()` 和 `mvp::logging::EnableFileLogging(const std::string& path)`，使用 MVP_CORE_EXPORT 导出
- [x] 2.2 新建 `src/core/src/logging.cc`：实现 Init()（创建 stderr console sink，设置日志格式含时间戳/级别/文件名:行号/消息）和 EnableFileLogging()（添加 file sink 到现有 logger）
- [x] 2.3 验证构建通过

## 3. 核心库关键操作日志

- [x] 3.1 demuxer.cc：Open 成功时 info 日志（文件路径、流数量、时长），失败时 error 日志；Close 时 info 日志
- [x] 3.2 decoder.cc：Open 成功时 info 日志（编解码器名称），失败时 error 日志
- [x] 3.3 audio_output.cc：Open 成功时 info 日志，失败时 error 日志（含 SDL 错误信息）
- [x] 3.4 player.cc：Open / Close / Play / Pause / Seek 时输出 info 日志；线程启停时 info 日志
- [x] 3.5 packet_queue.cc / frame_queue.cc：Abort 时输出 info 日志

## 4. UI 层用户操作日志

- [x] 4.1 main.cc：程序启动时调用 `mvp::logging::Init()`
- [x] 4.2 main_window.cc：打开文件时 info 日志（含路径）、播放/暂停按钮点击时 info 日志、Seek 操作时 info 日志（含目标时间）

## 5. 验证

- [x] 5.1 完整构建验证（cmake --preset default + cmake --build build 无错误）
- [x] 5.2 运行 mvp_app.exe，确认控制台可见日志输出
