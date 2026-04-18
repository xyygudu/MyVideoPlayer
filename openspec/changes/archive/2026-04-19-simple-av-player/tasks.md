## 1. 项目骨架与构建系统

- [x] 1.1 创建顶层 CMakeLists.txt，设置 C++17、项目名称，add_subdirectory(src/core) 和 add_subdirectory(src/app)
- [x] 1.2 创建 cmake/ 目录，编写 FindFFmpeg.cmake 和 FindSDL3.cmake 模块
- [x] 1.3 创建 src/core/CMakeLists.txt，构建 mvp_core 动态库，链接 FFmpeg 和 SDL3
- [x] 1.4 创建 src/app/CMakeLists.txt，构建 mvp_app 可执行文件，链接 mvp_core 和 Qt6
- [x] 1.5 创建 CMakePresets.json，配置 MSVC 编译器和依赖库路径，支持 VS Code 调试和 VS2026 工程生成
- [x] 1.6 验证 CMake 配置成功，能生成项目并编译空壳目标

## 2. 核心库基础设施

- [x] 2.1 创建 src/core/include/mvp/player.h，定义 Player 类公开接口（Open/Close/Play/Pause/Seek/Duration/CurrentPosition/IsPlaying/SetVideoFrameCallback）
- [x] 2.2 创建 src/core/src/packet_queue.h/.cc，实现线程安全的 PacketQueue（mutex + condition_variable，最大容量限制，push/pop/flush）
- [x] 2.3 创建 src/core/src/frame_queue.h/.cc，实现线程安全的 FrameQueue（mutex + condition_variable，最大容量限制，push/pop/flush）

## 3. 解复用与解码

- [x] 3.1 创建 src/core/src/demuxer.h/.cc，实现 Demuxer 类：打开文件（avformat_open_input + avformat_find_stream_info）、查找音视频流、demux 线程循环读取 packet 并分发到对应 PacketQueue
- [x] 3.2 创建 src/core/src/decoder.h/.cc，实现 Decoder 类：初始化解码器上下文、从 PacketQueue 取 packet 解码为 AVFrame、输出到 FrameQueue 或回调
- [x] 3.3 在 video decoder 中集成 sws_scale，将解码帧转为 RGB32 格式

## 4. 音频输出与音频时钟

- [x] 4.1 创建 src/core/src/audio_output.h/.cc，实现 AudioOutput 类：初始化 SDL3 音频设备、设置音频回调函数、暂停/恢复音频输出
- [x] 4.2 在 SDL 音频回调中从 audio FrameQueue（或直接解码）获取 PCM 数据填充缓冲区，并更新 audio_clock
- [x] 4.3 创建 src/core/src/clock.h/.cc，实现 Clock 类：set/get 时钟值，线程安全读写

## 5. 音视频同步与渲染调度

- [x] 5.1 实现视频渲染循环：从 video FrameQueue 取帧，比较 frame_pts 与 audio_clock，决定立即显示、等待或丢帧
- [x] 5.2 通过 VideoFrameCallback 将 RGB 帧数据回调给上层

## 6. 播放控制

- [x] 6.1 在 Player 类中整合 Demuxer、Decoder、AudioOutput、Clock，实现 Open（初始化所有组件）和 Close（停止线程、释放资源）
- [x] 6.2 实现 Play/Pause：控制 SDL 音频暂停/恢复、线程暂停/恢复标志
- [x] 6.3 实现 Seek：av_seek_frame + 清空队列 + flush 解码器 + 重置时钟
- [x] 6.4 实现 Duration 和 CurrentPosition 查询接口

## 7. Qt UI 界面

- [x] 7.1 创建 src/app/src/main.cc，初始化 QApplication，创建主窗口并显示
- [x] 7.2 创建 src/app/src/video_widget.h/.cc，实现 VideoWidget（继承 QWidget），通过 paintEvent + QImage 渲染视频帧，保持宽高比
- [x] 7.3 创建 src/app/src/main_window.h/.cc，实现 MainWindow：上部 VideoWidget + 下部控制栏（QHBoxLayout 包含播放/暂停按钮、进度 QSlider、时间 QLabel）
- [x] 7.4 连接播放/暂停按钮信号到 Player::Play/Pause，切换按钮图标
- [x] 7.5 连接进度条 sliderReleased 信号到 Player::Seek，使用 QTimer 定时更新进度条位置和时间标签
- [x] 7.6 实现打开文件功能：菜单栏或按钮触发 QFileDialog，选择文件后调用 Player::Open + Play

## 8. 集成验证

- [x] 8.1 完整编译通过，在 VS Code 中通过 CMake Tools 配置并调试运行
- [x] 8.2 使用 cmake -G "Visual Studio 18 2026" 生成 VS 工程，验证可在 VS 中编译调试
- [x] 8.3 播放一个测试视频文件，验证音视频同步、播放/暂停、Seek 功能正常
