# test-suite Specification

## Purpose
Defines the automated test suite: GoogleTest unit and integration tests
linked against the core objects, test media generated at build time with
ffmpeg, player tests observed only through the public API, and an opt-in
pass over a folder of real videos.

## Requirements
### Requirement: GoogleTest 测试套件
仓库 SHALL 提供 `tests/` 下的 `mvp_tests` 可执行文件（`MVP_BUILD_TESTS`，默认 ON）。GoogleTest SHALL 通过 FetchContent 以固定版本和 SHA256 获取。测试 SHALL 链接与 `mvp_core.dll` 相同的 `mvp_core_objects` 目标文件并定义 `MVP_CORE_STATIC_DEFINE`，以便测试内部类；测试 SHALL 用 `gtest_discover_tests` 注册到 CTest。

#### Scenario: 构建并运行
- **WHEN** 执行 `cmake --build --preset default` 后运行 `build/bin/mvp_tests.exe`
- **THEN** 全部测试执行，日志写入 `build/tests/mvp_tests.log` 而不刷屏

### Requirement: 测试素材在构建期生成
测试素材 SHALL 由 `FFMPEG_ROOT/bin` 中的 `ffmpeg` 在构建期用 lavfi 合成（`tests/generate_test_media.cmake`），不入库；找不到 ffmpeg 时配置期 SHALL 报出明确错误并提示 `-DMVP_BUILD_TESTS=OFF`。素材 SHALL 覆盖：带 B 帧的 H.264/AAC、2fps、时间戳起点 10 秒的 TS、时间戳跳变的拼接 TS、中途变分辨率/采样率的 TS、已知颜色的有限/全范围与 yuvj 片段、HEVC 10-bit、AV1、VP9/Opus、MPEG-2/MP2、MJPEG/PCM。

#### Scenario: 修改生成脚本后重新生成
- **WHEN** 修改 `generate_test_media.cmake` 后构建
- **THEN** 素材被重新生成，未修改时不重复生成

### Requirement: 播放集成测试经公共 API 观察
播放测试 SHALL 渲染到不显示的 Win32 窗口，等待期间泵送窗口消息；音频 SHALL 默认使用 SDL dummy 驱动（可被 `SDL_AUDIO_DRIVER` 环境变量覆盖）。断言 SHALL 只依赖公共 API：`State()`、`CurrentPosition()`、`GetPipelineInfo()`、`CaptureFrame()`、`EffectInfos()`。特效测试 SHALL 覆盖三条处理路径：GPU、下载后的 CPU（NV12）、软件解码（yuv420p）。

#### Scenario: 无硬解的机器
- **WHEN** 在不支持 D3D11VA 的机器上运行
- **THEN** GPU 路径的测试以软件路径运行并仍然通过

### Requirement: 可选的真实素材测试
`UserMediaTest` SHALL 在设置环境变量 `MVP_USER_MEDIA_DIR` 时，对该目录下每个视频文件（mp4/mov/mkv/avi/webm/ts/m2ts/flv/wmv）执行打开、播放、seek 到中点并检查出帧；未设置时 SHALL 跳过。

#### Scenario: 对本地素材目录运行
- **WHEN** `MVP_USER_MEDIA_DIR=C:\Users\acer\Videos\codec` 时运行 `mvp_tests --gtest_filter=UserMediaTest.*`
- **THEN** 目录中每个视频都能起播并在 seek 后显示目标位置的画面

