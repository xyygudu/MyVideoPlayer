## ADDED Requirements

### Requirement: 日志初始化
系统 SHALL 提供 `mvp::logging::Init()` 函数，调用后默认启用控制台日志输出。日志格式 SHALL 包含时间戳（精确到毫秒）、日志级别、源文件名与行号、消息内容。

#### Scenario: 默认初始化
- **WHEN** 调用 `mvp::logging::Init()` 且未指定参数
- **THEN** 后续所有日志输出 SHALL 打印到标准错误（stderr）控制台

#### Scenario: 重复初始化
- **WHEN** 多次调用 `mvp::logging::Init()`
- **THEN** SHALL 安全忽略后续调用，不产生重复 sink

### Requirement: 文件日志输出
系统 SHALL 提供 `mvp::logging::EnableFileLogging(const std::string& path)` 函数，启用后日志同时写入指定文件。

#### Scenario: 启用文件日志
- **WHEN** 调用 `mvp::logging::EnableFileLogging("app.log")`
- **THEN** 后续日志 SHALL 同时输出到控制台和 `app.log` 文件

#### Scenario: 文件路径无效
- **WHEN** 调用 `mvp::logging::EnableFileLogging` 且路径不可写
- **THEN** SHALL 在控制台输出一条 error 级别日志并继续运行，不崩溃

### Requirement: 核心库关键操作日志
mvp_core 中以下操作 SHALL 输出日志：

- Demuxer::Open 成功时输出 info 级别日志（含文件路径、流数量、时长）
- Demuxer::Open 失败时输出 error 级别日志（含文件路径与错误原因）
- Demuxer::Close 时输出 info 级别日志
- Decoder::Open 成功/失败时输出 info/error 级别日志（含编解码器名称）
- AudioOutput::Open 成功/失败时输出 info/error 级别日志
- Player::Open / Close / Play / Pause / Seek 时输出 info 级别日志
- PacketQueue::Abort / FrameQueue::Abort 时输出 info 级别日志

#### Scenario: 打开有效媒体文件
- **WHEN** 调用 Player::Open 传入有效文件路径
- **THEN** 控制台 SHALL 依次显示 Demuxer open、Decoder open、AudioOutput open、Player open 的 info 日志

#### Scenario: 打开无效媒体文件
- **WHEN** 调用 Player::Open 传入不存在的文件路径
- **THEN** 控制台 SHALL 显示 Demuxer open 的 error 日志，含文件路径

#### Scenario: 关闭播放器
- **WHEN** 调用 Player::Close
- **THEN** 控制台 SHALL 显示 queue abort、player close 等 info 日志

### Requirement: UI 层用户操作日志
mvp_app 中以下用户操作 SHALL 输出 info 级别日志：

- 用户点击"打开"按钮并选择文件（含文件路径）
- 用户点击播放/暂停按钮
- 用户拖拽进度条完成 Seek（含目标时间）

#### Scenario: 用户打开文件
- **WHEN** 用户通过文件对话框选择视频文件
- **THEN** 控制台 SHALL 显示 info 日志，含所选文件的完整路径

### Requirement: 构建系统集成 spdlog
CMake 构建系统 SHALL 通过 `find_package(spdlog REQUIRED)` 引入 spdlog 库。CMakePresets.json 中 default 和 vs2026 两个 preset SHALL 都包含 spdlog 的查找路径。

#### Scenario: Ninja 构建成功
- **WHEN** 执行 `cmake --preset default && cmake --build build`
- **THEN** 构建 SHALL 成功完成，mvp_core.dll 和 mvp_app.exe 均链接 spdlog

#### Scenario: VS 工程生成成功
- **WHEN** 使用 vs2026 preset 生成 Visual Studio 工程
- **THEN** 生成 SHALL 成功，解决方案中可正常编译
