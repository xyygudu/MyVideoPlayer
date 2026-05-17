## MODIFIED Requirements

### Requirement: VideoFrame class encapsulates decoded video data
系统 SHALL 提供公共 API 类 `mvp::VideoFrame`，封装解码后的视频帧数据。VideoFrame SHALL 不依赖任何 FFmpeg 头文件。VideoFrame SHALL 为 move-only 语义（禁止拷贝，支持移动）。

VideoFrame SHALL 暴露以下只读访问器：
- `data(int plane)` → 返回指定平面的像素数据指针
- `linesize(int plane)` → 返回指定平面的行字节步长
- `width()` / `height()` → 帧的像素宽高
- `format()` → 像素格式枚举（如 YUV420P）
- `pts()` → 显示时间戳（秒）

VideoFrame 析构时 SHALL 自动释放其持有的内部缓冲区引用。

VideoFrame SHALL 可通过内部工厂函数 `MakeVideoFrame(MediaFrame&)` 从 MediaFrame 构造（仅 core/src 内部可见）。

#### Scenario: Create VideoFrame from MediaFrame at render boundary
- **WHEN** 渲染层从 FrameQueue 弹出 MediaFrame 并调用 MakeVideoFrame
- **THEN** VideoFrame 持有帧数据的有效引用，format 正确映射为 PixelFormat 枚举

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

AudioFrame SHALL 可通过内部工厂函数 `MakeAudioFrame(MediaFrame&)` 从 MediaFrame 构造（仅 core/src 内部可见）。

#### Scenario: Create AudioFrame from MediaFrame at render boundary
- **WHEN** 音频渲染器从 FrameQueue 弹出 MediaFrame 并调用 MakeAudioFrame
- **THEN** AudioFrame 持有帧数据的有效引用，format 正确映射为 SampleFormat 枚举

#### Scenario: AudioFrame move semantics
- **WHEN** AudioFrame 被 move 到另一个 AudioFrame 对象
- **THEN** 源对象变为空，目标对象持有数据

### Requirement: Format mapping resides at render boundary
像素格式映射（AVPixelFormat → PixelFormat）和采样格式映射（AVSampleFormat → SampleFormat）SHALL 在 `MakeVideoFrame` / `MakeAudioFrame` 工厂函数中执行，不在管线传输层（StreamContext/Decoder）中执行。

#### Scenario: Pipeline does not perform format mapping
- **WHEN** MediaFrame 在管线中传输（Decoder → Queue → Pop）
- **THEN** MediaFrame 不携带 PixelFormat/SampleFormat 枚举，仅在构造 VideoFrame/AudioFrame 时映射

#### Scenario: Render boundary performs complete mapping
- **WHEN** MakeVideoFrame 被调用
- **THEN** 从 AVFrame::format 整数值映射为 PixelFormat 枚举（YUV420P/NV12/D3D11 等）
