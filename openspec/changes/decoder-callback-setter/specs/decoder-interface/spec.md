## ADDED Requirements

### Requirement: IDecoder provides SetFrameCallback setter
IDecoder SHALL 提供纯虚方法 `virtual void SetFrameCallback(MediaFrameCallback cb) = 0`。调用方 SHALL 在 Start() 之前设置回调。实现类 SHALL 存储回调并在解码出帧时调用。

#### Scenario: SetFrameCallback before Start
- **WHEN** 调用方在 Start() 之前调用 SetFrameCallback(cb)
- **THEN** 解码出帧时通过 cb 输出 MediaFrame

#### Scenario: SetFrameCallback not called before Start
- **WHEN** 调用方未调用 SetFrameCallback 就调用 Start()
- **THEN** 触发 assert 失败（开发期错误检测）

### Requirement: IDecoder provides SetEofCallback setter
IDecoder SHALL 提供纯虚方法 `virtual void SetEofCallback(EofOutputCallback cb) = 0`。调用方 SHALL 在 Start() 之前设置回调。实现类 SHALL 存储回调并在流结束时调用。

#### Scenario: SetEofCallback before Start
- **WHEN** 调用方在 Start() 之前调用 SetEofCallback(cb)
- **THEN** 流结束时通过 cb 通知 EOF

#### Scenario: SetEofCallback not called before Start
- **WHEN** 调用方未调用 SetEofCallback 就调用 Start()
- **THEN** 触发 assert 失败（开发期错误检测）

## MODIFIED Requirements

### Requirement: IDecoder defines abstract decoder interface
系统 SHALL 定义 `IDecoder` 抽象接口类，提供以下纯虚方法：
- `virtual bool Open(AVStream* stream, HWAccelContext* hw_ctx = nullptr) = 0`：初始化解码器
- `virtual void SetFrameCallback(MediaFrameCallback cb) = 0`：设置帧输出回调
- `virtual void SetEofCallback(EofOutputCallback cb) = 0`：设置 EOF 回调
- `virtual void Start(PacketQueue* packet_queue) = 0`：启动解码线程
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

### Requirement: AVFrameDecoder implements IDecoder
系统 SHALL 提供 `AVFrameDecoder` 类实现 `IDecoder` 接口。AVFrameDecoder SHALL：
- 使用 FFmpeg 的 `avcodec_send_packet` / `avcodec_receive_frame` API 进行解码
- 在 `Open()` 时从 `AVStream::codecpar->codec_type` 确定 MediaType 并缓存
- 通过 `SetFrameCallback` 和 `SetEofCallback` 接收回调并存储为成员变量
- 在 `Start()` 启动解码线程前 assert 回调已设置
- 在解码出帧时构造 `MediaFrame(raw_frame, pts, media_type_)` 并通过存储的回调输出
- 保持现有的 serial 变更检测、codec flush、skip_frame、drop_until_pts 等行为不变

#### Scenario: AVFrameDecoder decodes video stream
- **WHEN** AVFrameDecoder 以视频 AVStream 调用 Open()，SetFrameCallback，SetEofCallback，然后 Start()
- **THEN** 解码线程启动，持续从 PacketQueue 消费，输出 MediaFrame(type=kVideo)

#### Scenario: AVFrameDecoder decodes audio stream
- **WHEN** AVFrameDecoder 以音频 AVStream 调用 Open()，设置回调，然后 Start()
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
