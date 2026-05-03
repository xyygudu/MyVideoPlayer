## ADDED Requirements

### Requirement: VideoFrame class encapsulates decoded video data
系统 SHALL 提供公共 API 类 `mvp::VideoFrame`，封装解码后的视频帧数据。VideoFrame SHALL 不依赖任何 FFmpeg 头文件。VideoFrame SHALL 为 move-only 语义（禁止拷贝，支持移动）。

VideoFrame SHALL 暴露以下只读访问器：
- `data(int plane)` → 返回指定平面的像素数据指针
- `linesize(int plane)` → 返回指定平面的行字节步长
- `width()` / `height()` → 帧的像素宽高
- `format()` → 像素格式枚举（如 YUV420P）
- `pts()` → 显示时间戳（秒）

VideoFrame 析构时 SHALL 自动释放其持有的内部缓冲区引用。

#### Scenario: Create VideoFrame from decoded data
- **WHEN** Decoder 解码出一帧视频并构造 VideoFrame
- **THEN** VideoFrame 持有帧数据的有效引用，外部可通过访问器读取 Y/U/V 平面

#### Scenario: VideoFrame move semantics
- **WHEN** VideoFrame 被 move 到另一个 VideoFrame 对象
- **THEN** 源对象变为空（data 返回 nullptr），目标对象持有数据

#### Scenario: VideoFrame destruction releases buffer
- **WHEN** VideoFrame 对象析构
- **THEN** 内部缓冲区引用计数减一，若归零则释放内存

### Requirement: AudioFrame class encapsulates decoded audio data
系统 SHALL 提供公共 API 类 `mvp::AudioFrame`，封装解码后的音频帧数据。AudioFrame SHALL 不依赖任何 FFmpeg 头文件。AudioFrame SHALL 为 move-only 语义。

AudioFrame SHALL 暴露以下只读访问器：
- `data()` → 音频采样数据指针
- `nb_samples()` → 采样数
- `channels()` → 声道数
- `sample_rate()` → 采样率
- `format()` → 采样格式枚举
- `pts()` → 显示时间戳（秒）

AudioFrame 析构时 SHALL 自动释放其持有的内部缓冲区引用。

#### Scenario: Create AudioFrame from decoded data
- **WHEN** Decoder 解码出一帧音频并构造 AudioFrame
- **THEN** AudioFrame 持有帧数据的有效引用，外部可通过访问器读取 PCM 数据

#### Scenario: AudioFrame move semantics
- **WHEN** AudioFrame 被 move 到另一个 AudioFrame 对象
- **THEN** 源对象变为空，目标对象持有数据

### Requirement: AVFramePtr provides RAII for AVFrame
系统内部 SHALL 提供 `AVFramePtr` 类（不暴露在公共 API），以 RAII 方式管理 `AVFrame` 生命周期。

- 构造时 SHALL 调用 `av_frame_alloc()`
- 析构时 SHALL 调用 `av_frame_free()`
- SHALL 支持 move 语义，禁止拷贝
- SHALL 提供 `get()` 返回原始指针、`unref()` 调用 `av_frame_unref()`

#### Scenario: AVFramePtr auto-frees on destruction
- **WHEN** AVFramePtr 离开作用域
- **THEN** 内部 AVFrame 被自动释放（av_frame_free），无内存泄漏

#### Scenario: AVFramePtr move transfers ownership
- **WHEN** AVFramePtr 被 move 赋值给另一个 AVFramePtr
- **THEN** 源指针置空，目标持有原始 AVFrame

### Requirement: AVPacketPtr provides RAII for AVPacket
系统内部 SHALL 提供 `AVPacketPtr` 类（不暴露在公共 API），以 RAII 方式管理 `AVPacket` 生命周期。

- 构造时 SHALL 调用 `av_packet_alloc()`
- 析构时 SHALL 调用 `av_packet_free()`
- SHALL 支持 move 语义，禁止拷贝
- SHALL 提供 `get()` 返回原始指针、`unref()` 调用 `av_packet_unref()`

#### Scenario: AVPacketPtr auto-frees on destruction
- **WHEN** AVPacketPtr 离开作用域
- **THEN** 内部 AVPacket 被自动释放，无内存泄漏

#### Scenario: AVPacketPtr move transfers ownership
- **WHEN** AVPacketPtr 被 move 赋值给另一个 AVPacketPtr
- **THEN** 源指针置空，目标持有原始 AVPacket
