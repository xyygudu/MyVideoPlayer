# settings-ui Specification

## Purpose
Defines the settings page: runtime pipeline switches (hardware decode, GPU
effects, zero-copy) persisted with QSettings, live pipeline diagnostics,
logging control and a screenshot of the frame on screen — the tools for
comparing the GPU and CPU paths while debugging.

## Requirements
### Requirement: 设置页提供解码与渲染开关
SettingsPage 的「解码与渲染」分组 SHALL 提供三个复选框：「硬件解码 (D3D11VA)」「GPU 特效处理」「硬件帧零拷贝呈现」，分别对应 `PlayerOptions` 的 `hardware_decode`、`gpu_effects`、`zero_copy`。勾选变化 SHALL 立即调用 `MediaPlayer::SetOptions()`（已打开的文件在当前位置重建管线）。关闭硬件解码时，后两个只与硬件帧相关的开关 SHALL 置灰。

#### Scenario: 关闭硬件解码
- **WHEN** 播放中取消勾选「硬件解码 (D3D11VA)」
- **THEN** 播放在原位置继续，诊断区「解码方式」显示「CPU (软件解码)」

#### Scenario: 关闭 GPU 特效
- **WHEN** 取消勾选「GPU 特效处理」
- **THEN** 硬件帧下载到内存后由 CPU 执行特效，画面效果与 GPU 路径一致

### Requirement: 设置持久化
三个管线开关与日志级别 SHALL 通过 `QSettings`（组织名/应用名均为 MyVideoPlayer）持久化，应用启动构造设置页时 SHALL 读取并应用到播放器与日志系统。

#### Scenario: 重启后保持
- **WHEN** 关闭「硬件解码」后重启应用并打开文件
- **THEN** 该文件以软件解码播放，复选框保持未勾选

### Requirement: 设置页显示当前管线
「当前管线」分组 SHALL 显示播放状态、视频编码、解码方式（GPU (D3D11VA) / CPU (软件解码)）、渲染后端、已呈现帧数、播放位置/时长/帧率。数据 SHALL 只在页面可见时每 500ms 刷新一次。

#### Scenario: 播放中查看
- **WHEN** 播放 H.264 文件时打开设置页
- **THEN** 已呈现帧数持续增长，解码方式与实际一致

### Requirement: 设置页提供日志控制
「日志」分组 SHALL 提供日志级别下拉框（Trace/Debug/Info/Warn/Error，调用 `logging::SetLevel`）、「输出到控制台」复选框（`SetConsoleLoggingEnabled`）、当前日志文件路径（未启用时显示「未启用」）与「打开日志目录」按钮（系统文件管理器打开）。

#### Scenario: 打开日志目录
- **WHEN** 文件日志已启用，点击「打开日志目录」
- **THEN** 系统文件管理器打开日志文件所在目录

### Requirement: 设置页提供当前帧截图
「调试」分组 SHALL 提供「保存当前帧截图…」按钮：调用 `MediaPlayer::CaptureFrame()`，无画面时提示「没有可截图的画面」；否则弹出保存对话框（默认文件名含帧 pts），保存为 PNG 并显示保存结果。

#### Scenario: 保存截图
- **WHEN** 播放中点击「保存当前帧截图…」并选择路径
- **THEN** 生成与屏幕画面（含特效）一致的 PNG，状态文字显示保存路径

