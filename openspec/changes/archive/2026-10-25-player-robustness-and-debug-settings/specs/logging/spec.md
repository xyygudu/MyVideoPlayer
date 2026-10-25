## ADDED Requirements

### Requirement: 运行期日志级别与日志文件查询
`mvp::logging` SHALL 提供：
- `enum class Level { kTrace, kDebug, kInfo, kWarn, kError }`、`SetLevel(Level)` / `GetLevel()`：同时作用于核心库日志与桥接进来的 FFmpeg 日志（`av_log_set_level` 取对应级别）
- `IsConsoleLoggingEnabled()`：查询控制台输出开关
- `FileLogPath()`：`EnableFileLogging` 打开的文件路径，未启用文件日志时为空

#### Scenario: 调到 Debug 级别
- **WHEN** 调用 `SetLevel(Level::kDebug)`
- **THEN** 核心库的 debug 日志与 FFmpeg 的 verbose/debug 诊断开始输出

#### Scenario: 查询日志文件
- **WHEN** 应用以文件日志启动后调用 `FileLogPath()`
- **THEN** 返回该日志文件路径
