## ADDED Requirements

### Requirement: IDecoder defines abstract decoder interface
系统 SHALL 定义 `IDecoder` 抽象接口类，提供以下纯虚方法：
- `virtual bool Open(AVStream* stream, HWAccelContext* hw_ctx = nullptr) = 0`：初始化解码器
- `virtual void Start(PacketQueue* packet_queue, MediaFrameCallback on_frame, EofOutputCallback on_eof) = 0`：启动解码线程
- `virtual void Stop() = 0`：停止解码线程（阻塞直到线程退出）
- `virtual void SetDropUntilPts(double pts) = 0`：设置 seek 快速跳帧目标
- `virtual ~IDecoder() = default`：虚析构

IDecoder SHALL 不依赖具体帧类型（VideoFrame/AudioFrame），仅依赖 MediaFrame。

#### Scenario: IDecoder is pure interface
- **WHEN** 系统需要创建解码器实例
- **THEN** 通过 IDecoder 接口引用，不感知具体实现类

#### Scenario: IDecoder supports polymorphic destruction
- **WHEN** 通过 unique_ptr<IDecoder> 持有的解码器被销毁
- **THEN** 正确调用派生类析构函数，无资源泄漏

### Requirement: MediaFrameCallback signature outputs MediaFrame
系统 SHALL 定义回调类型：
```cpp
using MediaFrameCallback = std::function<void(MediaFrame frame, int serial)>;
```

IDecoder 实现 SHALL 在解码出帧时通过此回调输出 MediaFrame（携带正确的 MediaType、pts）。

#### Scenario: Callback delivers MediaFrame with correct type
- **WHEN** AVFrameDecoder 解码出一帧视频
- **THEN** 回调被调用，传入的 MediaFrame 中 type()==MediaType::kVideo，pts 为正确的秒数

#### Scenario: Callback delivers audio frame
- **WHEN** AVFrameDecoder 解码出一帧音频
- **THEN** 回调被调用，传入的 MediaFrame 中 type()==MediaType::kAudio

### Requirement: AVFrameDecoder implements IDecoder
系统 SHALL 提供 `AVFrameDecoder` 类实现 `IDecoder` 接口。AVFrameDecoder SHALL：
- 使用 FFmpeg 的 `avcodec_send_packet` / `avcodec_receive_frame` API 进行解码
- 在 `Open()` 时从 `AVStream::codecpar->codec_type` 确定 MediaType 并缓存
- 在解码出帧时构造 `MediaFrame(raw_frame, pts, media_type_)` 并通过回调输出
- 保持现有的 serial 变更检测、codec flush、skip_frame、drop_until_pts 等行为不变

#### Scenario: AVFrameDecoder decodes video stream
- **WHEN** AVFrameDecoder 以视频 AVStream 调用 Open()，然后 Start()
- **THEN** 解码线程启动，持续从 PacketQueue 消费，输出 MediaFrame(type=kVideo)

#### Scenario: AVFrameDecoder decodes audio stream
- **WHEN** AVFrameDecoder 以音频 AVStream 调用 Open()，然后 Start()
- **THEN** 解码线程启动，输出 MediaFrame(type=kAudio)

#### Scenario: AVFrameDecoder supports hardware acceleration
- **WHEN** Open() 时传入有效的 HWAccelContext
- **THEN** 解码使用硬件加速，输出的 MediaFrame 内部 AVFrame 为硬件帧格式

#### Scenario: AVFrameDecoder flushes on serial change
- **WHEN** 从 PacketQueue pop 到 serial 变更的 packet
- **THEN** AVFrameDecoder 执行 avcodec_flush_buffers，行为与原 Decoder 一致

#### Scenario: AVFrameDecoder drops frames before target pts
- **WHEN** SetDropUntilPts(5.0) 被调用且解码出帧 pts < 5.0
- **THEN** 该帧不通过回调输出，直接 unref

### Requirement: EofOutputCallback remains unchanged
EOF 回调签名 SHALL 保持为 `std::function<void(int serial)>`，IDecoder 实现在流结束时调用。

#### Scenario: EOF callback invoked at stream end
- **WHEN** Decoder 检测到解码流结束（receive_frame 返回 AVERROR_EOF）
- **THEN** 调用 EofOutputCallback 传入当前 serial
