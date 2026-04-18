## Context

当前项目为空白工程，目标是构建一个学习向的简单音视频播放器。底层使用 FFmpeg 7.1.1 进行解复用与解码，SDL3 负责音频输出，Qt 6.7.3 构建 UI。编译器为 MSVC (VS2026)，构建系统为 CMake，需同时支持 VS Code 调试和生成 VS 工程。

约束条件：
- 底层引擎（`mvp_core`）必须与 UI 分离，可独立编译为动态库
- Google C++ Style（缩进改为 4 空格，详见 .clang-format）
- 跨平台架构（首先 Windows 验证）
- 保持简单，不过度工程化，后续逐步迭代

## Goals / Non-Goals

**Goals:**
- 实现可播放常见格式视频文件的播放器
- 音视频同步以音频时钟为基准
- 支持播放、暂停、Seek 操作
- 底层库可独立编译和复用
- CMake 支持 VS Code 调试 + VS2026 工程生成

**Non-Goals:**
- 不实现播放列表、字幕、多音轨切换
- 不实现硬件加速解码（首版纯软解）
- 不实现网络流播放（仅本地文件）
- 不需要单元测试
- 不做视频滤镜/特效

## Decisions

### 1. 项目结构：双目标 CMake 工程

```
MyVideoPlayer/
├── CMakeLists.txt              # 顶层
├── cmake/                      # FindXxx.cmake 模块
├── src/
│   ├── core/                   # mvp_core 动态库
│   │   ├── CMakeLists.txt
│   │   ├── include/mvp/        # 公开头文件
│   │   └── src/                # 内部实现
│   └── app/                    # mvp_app Qt 可执行文件
│       ├── CMakeLists.txt
│       └── src/
└── openspec/
```

**理由**：`core` 独立编译为 shared library，`app` 链接 `core`。公开头文件放在 `include/mvp/` 下，方便第三方集成。

### 2. 核心架构：三线程模型

| 线程 | 职责 |
|------|------|
| Demux 线程 | 读取文件，解复用出 audio/video packet，分别推入 packet queue |
| Audio 解码+播放线程 | 从 audio packet queue 取包解码，通过 SDL 回调输出音频，维护音频时钟 |
| Video 解码+渲染线程 | 从 video packet queue 取包解码，放入 frame queue，根据音频时钟调度显示 |

**理由**：业界常规三线程模型，简洁易理解。音频由 SDL 回调驱动天然保持节奏，视频对齐音频时钟。

**备选方案**：
- 单线程轮询：太简单，无法保证实时性
- FFmpeg 多线程解码 + 单独渲染线程：过于复杂

### 3. 缓存队列设计

- **PacketQueue**: 存放 `AVPacket*`，使用 `std::mutex` + `std::condition_variable` 实现线程安全，设最大容量（如 256 个 packet），满时 demux 线程阻塞等待。
- **FrameQueue**: 存放已解码的 `AVFrame*`，容量较小（如 16 帧），用于 video 渲染线程按时取帧。

**理由**：有限容量队列防止内存膨胀，条件变量避免忙等。

### 4. 音视频同步策略

- 音频时钟：SDL 音频回调每次输出采样时更新时钟值（`audio_clock`）
- 视频显示：视频线程取出帧后比较 `frame_pts` 与 `audio_clock`
  - `frame_pts <= audio_clock`：立即显示
  - `frame_pts > audio_clock`：等待差值时间后显示
  - 差距过大（>阈值）：丢帧或重复帧

**理由**：以音频为主时钟是业界最常用方案，人耳对音频不连续更敏感。

### 5. Seek 实现

- 调用 `av_seek_frame()` 跳转到目标位置
- 清空所有 packet queue 和 frame queue
- 解码端在 Seek 后 flush codec（`avcodec_flush_buffers`）
- 重新建立音频时钟

### 6. 视频渲染方式

UI 层通过 `QWidget::paintEvent` + `QImage` 渲染视频帧。核心库解码后将帧使用 `sws_scale` 转为 RGB32 格式，通过回调传给 UI。

**备选方案**：
- QOpenGLWidget + shader YUV 渲染：性能更好但复杂，后续迭代
- SDL_Renderer：与 Qt 窗口集成困难

**理由**：QImage 方式最简单，学习阶段够用，后续可升级为 OpenGL。

### 7. 核心库 API 设计

```cpp
namespace mvp {
class Player {
 public:
  // 生命周期
  bool Open(const std::string& filepath);
  void Close();

  // 播放控制
  void Play();
  void Pause();
  void Seek(double position_seconds);

  // 状态查询
  double Duration() const;
  double CurrentPosition() const;
  bool IsPlaying() const;

  // 回调注册
  using VideoFrameCallback = std::function<void(const uint8_t* data, int width, int height)>;
  void SetVideoFrameCallback(VideoFrameCallback cb);
};
}  // namespace mvp
```

**理由**：简洁的面向对象接口，无 Qt/SDL 依赖泄漏到公共 API，通过回调将视频帧数据传给调用方。

### 8. 构建系统

- 顶层 CMakeLists.txt 设置 C++17，`add_subdirectory(src/core)` 和 `add_subdirectory(src/app)`
- `cmake/` 下放置 `FindFFmpeg.cmake`、`FindSDL3.cmake` 等模块
- 支持 CMake presets 或命令行传入依赖路径
- VS Code：使用 CMake Tools 扩展，配置 `cmake-kits.json` 指定 MSVC
- VS2026：`cmake -G "Visual Studio 18 2026" -A x64`

## Risks / Trade-offs

- **[QImage 渲染性能]** → 对于 1080p+ 视频可能帧率不足。缓解：后续迭代升级为 OpenGL 渲染。首版对学习目的已足够。
- **[纯软解 CPU 占用高]** → 高分辨率视频 CPU 占用大。缓解：后续加入硬件加速。
- **[SDL 音频回调线程安全]** → SDL 音频回调在独立线程运行，需确保对 packet queue 的访问线程安全。缓解：使用 mutex 保护。
- **[Seek 精度]** → `av_seek_frame` 可能跳转到关键帧而非精确位置。缓解：首版接受关键帧精度，后续可精确 Seek。
- **[跨平台音频后端]** → SDL3 已内建跨平台音频支持，风险较低。
