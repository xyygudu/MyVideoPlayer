# MyVideoPlayer

一个基于 FFmpeg + SDL3 + Qt6 构建的轻量级视频播放器，参考 FFplay / MPV / VLC 等主流播放器架构设计。

## 运行截图

![主界面](image/mainwindow.png)

## 功能特性

- 支持 FFmpeg 所支持的所有视频/音频格式
- 音视频同步（Audio Master 时钟驱动）
- 播放 / 暂停 / Seek / 逐帧步进
- SDL3 硬件加速视频渲染（嵌入 Qt 窗口）
- 状态机驱动的播放生命周期管理
- EOF 优雅传播与自动停止

## 架构概览

```
┌─────────────────────────────────────────────────┐
│                  Qt6 GUI (app)                   │
│         MainWindow / VideoWidget                 │
├─────────────────────────────────────────────────┤
│                mvp_core (shared lib)             │
│  ┌──────────┐  ┌─────────┐  ┌───────────────┐  │
│  │ Demuxer  │→ │ Decoder │→ │ FrameQueue    │  │
│  └──────────┘  └─────────┘  └───────┬───────┘  │
│                                      │          │
│  ┌──────────────────┐  ┌────────────┴────────┐ │
│  │ AudioRenderer    │  │ VideoRenderer       │  │
│  │ (SDL3 audio)     │  │ (SDL3 texture)      │  │
│  └──────────────────┘  └─────────────────────┘ │
│               ↕ Clock / Sync                    │
└─────────────────────────────────────────────────┘
```

## 依赖

| 库 | 版本要求 | 用途 |
|---|---|---|
| FFmpeg | >= 7.0 | 解封装、解码 |
| SDL3 | >= 3.2 | 音频播放、视频渲染 |
| Qt6 | >= 6.5 | GUI 框架 |
| spdlog | >= 1.12 | 日志 |
| CMake | >= 3.20 | 构建系统 |
| MSVC | 2022+ | 编译器（当前仅支持 Windows） |

## 构建方法

### 前置条件

1. 安装 [Visual Studio 2022](https://visualstudio.microsoft.com/)（含「使用 C++ 的桌面开发」工作负载）
2. 安装 [CMake](https://cmake.org/download/) >= 3.20
3. 下载以下依赖的预编译版本：
   - [FFmpeg Shared Build](https://github.com/BtbN/FFmpeg-Builds/releases)（选 `shared` 版本，解压即可）
   - [SDL3](https://github.com/libsdl-org/SDL/releases)（下载 `SDL3-devel-x.x.x-VC.zip`）
   - [Qt 6](https://www.qt.io/download)（安装时选择 MSVC 2022 64-bit 组件）
   - [spdlog](https://github.com/gabime/spdlog/releases)（需自行 CMake 编译安装，见下方说明）

#### spdlog 编译安装示例

```bash
git clone https://github.com/gabime/spdlog.git
cd spdlog && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX="C:/libs/spdlog" -DSPDLOG_BUILD_SHARED=OFF
cmake --build . --config Release
cmake --install .
```

### 方式一：VS Code + Ninja 构建

在项目根目录创建 `CMakeUserPresets.json`（此文件已在 `.gitignore` 中忽略，不会被提交）：

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "local",
      "displayName": "Local Dev",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "<你的Qt安装路径>/6.7.3/msvc2022_64",
        "FFMPEG_ROOT": "<你的FFmpeg解压路径>",
        "SDL3_DIR": "<你的SDL3解压路径>/cmake",
        "spdlog_DIR": "<你的spdlog安装路径>/lib/cmake/spdlog"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "local",
      "configurePreset": "local"
    }
  ]
}
```

> **注意**：需要在 **x64 Native Tools Command Prompt for VS 2022**（或 VS Code 中已配置 MSVC 环境的终端）下执行，以确保 `cl.exe` 在 PATH 中。

```bash
cmake --preset local
cmake --build build
```

### 方式二：Visual Studio IDE 构建

创建 `CMakeUserPresets.json`：

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "vs-local",
      "displayName": "VS 2022 x64",
      "generator": "Visual Studio 17 2022",
      "architecture": {
        "value": "x64",
        "strategy": "set"
      },
      "binaryDir": "${sourceDir}/build-vs",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "<你的Qt安装路径>/6.7.3/msvc2022_64",
        "FFMPEG_ROOT": "<你的FFmpeg解压路径>",
        "SDL3_DIR": "<你的SDL3解压路径>/cmake",
        "spdlog_DIR": "<你的spdlog安装路径>/lib/cmake/spdlog"
      }
    }
  ]
}
```

```bash
cmake --preset vs-local
```

然后用 Visual Studio 打开 `build-vs/MyVideoPlayer.sln`，选择 Debug/x64 配置编译即可。

### 路径填写说明

| 变量 | 指向什么 | 示例值 |
|------|---------|--------|
| `CMAKE_PREFIX_PATH` | Qt MSVC 编译版本的根目录（包含 `lib/cmake/Qt6`） | `D:/Qt/6.7.3/msvc2022_64` |
| `FFMPEG_ROOT` | FFmpeg shared build 解压后的根目录（包含 `bin/`, `lib/`, `include/`） | `D:/libs/ffmpeg-7.1.1-full_build-shared` |
| `SDL3_DIR` | SDL3 安装目录下的 `cmake` 子目录（包含 `SDL3Config.cmake`） | `D:/libs/SDL3-3.2.16/cmake` |
| `spdlog_DIR` | spdlog 安装目录下的 CMake 配置目录（包含 `spdlogConfig.cmake`） | `D:/libs/spdlog/lib/cmake/spdlog` |

> 路径使用正斜杠 `/` 或双反斜杠 `\\`，**不要**使用单反斜杠 `\`。

### 运行

编译成功后，可执行文件位于 `build/bin/mvp_app.exe`（Ninja）或 `build-vs/bin/Debug/mvp_app.exe`（VS），所需 DLL 会自动复制到同目录。

## 项目结构

```
src/
├── core/           # 播放器核心库 (mvp_core)
│   ├── include/    #   公开头文件
│   └── src/        #   实现：demuxer, decoder, renderer, clock...
└── app/            # Qt GUI 应用 (mvp_app)
    └── src/        #   MainWindow, VideoWidget

cmake/              # 自定义 CMake 模块 (FindFFmpeg.cmake)
docs/               # 设计文档
openspec/           # AI 辅助的 Spec-Driven 开发工作流
```

## 开发工作流（OpenSpec）

本项目使用 [OpenSpec](https://github.com/Fission-AI/OpenSpec) 工作流进行 AI 辅助的架构设计与任务管理。`openspec/` 目录结构：

```
openspec/
├── config.yaml         # 工作流配置
├── specs/              # 模块规格文档（长期维护）
│   ├── demux-decode/   #   解封装与解码模块规格
│   ├── av-sync/        #   音视频同步规格
│   ├── video-renderer/ #   视频渲染规格
│   └── ...
└── changes/            # 变更记录
    └── archive/        #   已完成的变更（含设计文档与任务清单）
```

**工作方式：**

1. **Spec（规格）** — 每个核心模块有一份 `spec.md`，描述模块职责、接口契约、同步策略等设计约束
2. **Change（变更）** — 每次功能开发/重构先生成 proposal + tasks，实现完毕后归档到 `changes/archive/`
3. **AI 协作** — 通过 Copilot Agent 读取 spec 上下文，确保代码实现符合架构设计

这套流程让每次改动都有据可查，也方便后续回顾设计决策的演进过程。
