## Purpose

Defines demuxing and decoding in the media pipeline: FrameQueue serial
semantics, the demux thread and Demuxer stream accessors, and how
DemuxNode / DecoderNode / AudioSinkNode configure themselves from port
formats and graph resources (including hardware decode via the graph GPU
device) and respond to seek.
## Requirements
### Requirement: FrameQueue supports serial
FrameQueue SHALL 为模板类 `FrameQueue<T>`，管线中 SHALL 实例化为 `FrameQueue<MediaFrame>`。

QueueEntry 定义 SHALL 为：
```cpp
template<typename T>
struct QueueEntry {
    T frame;
    int serial;
    bool eof{false};
};
```

Push 接口 SHALL 接收 `QueueEntry<T>` 值参数，通过 move 语义获取 frame 所有权。

Pop 接口 SHALL 返回 `std::optional<QueueEntry<T>>`。返回 `nullopt` 表示队列已 abort。调用方通过 `QueueEntry::eof` 判断是否为 EOF 标记。

`PushEof(int serial)` SHALL 推入一个 `eof=true` 的 QueueEntry（frame 为默认构造）。

FrameQueue SHALL 维护一个 `serial` 计数器（初始为 0）。Push 由调用方在 `QueueEntry::serial` 中显式传入 serial 值。

接口 SHALL 分离为三个独立方法：
- `Flush()`：清空队列数据 + 递增 serial。不改变 abort 状态。
- `Abort()`：设 abort=true + 唤醒所有等待线程。不清空数据。
- `Reset()`：重置为初始状态。

#### Scenario: Flush only clears data and increments serial
- **WHEN** 调用 FrameQueue<MediaFrame>::Flush()
- **THEN** 队列数据被清空，serial 递增，abort 状态不变

#### Scenario: Abort only signals termination
- **WHEN** 调用 FrameQueue<MediaFrame>::Abort()
- **THEN** abort=true，所有等待线程被唤醒

#### Scenario: Serial increments on Flush
- **WHEN** 调用 Flush()
- **THEN** serial 值递增

#### Scenario: Render discards stale frames
- **WHEN** Pop 返回的 QueueEntry serial 不等于 StreamContext::CurrentSerial()
- **THEN** render 线程丢弃该 frame 并继续 Pop 下一帧

### Requirement: Demux thread produces packets
系统 SHALL 在独立的 demux 线程中持续读取 packet，并将音频 packet 和视频 packet 分别推入对应的 PacketQueue。Demuxer SHALL 维护本地 serial 副本，仅在 seek 完成后更新为最新值，确保 seek 前的旧 packet 保留旧 serial。

#### Scenario: Packets are dispatched to correct queues
- **WHEN** demux 线程读取到一个 audio packet
- **THEN** 该 packet 被推入 audio PacketQueue，携带当前本地 serial 值

#### Scenario: Demux blocks when queue is full
- **WHEN** 目标 PacketQueue 已达到最大字节数上限
- **THEN** demux 线程阻塞等待，直到队列字节数低于上限

#### Scenario: Demux updates serial after seek
- **WHEN** demux 线程处理完 seek 请求（av_seek_frame 返回后）
- **THEN** demux 更新本地 serial 副本为 packet queue 的最新 serial 值

### Requirement: Demuxer provides typed stream accessors
Demuxer SHALL 提供以下访问器方法，替代 `FormatContext()` 的公开暴露：
- `AVStream* AudioStream() const`：返回音频流指针（无音频时返回 nullptr）
- `AVStream* VideoStream() const`：返回视频流指针（无视频时返回 nullptr）

Demuxer SHALL 不再公开 `FormatContext()` 方法。内部需要 `AVFormatContext` 的操作 SHALL 在 Demuxer 内部完成。

#### Scenario: AudioStream returns valid pointer
- **WHEN** 文件包含音频流且 Demuxer 已 Open
- **THEN** AudioStream() 返回有效的 AVStream*

#### Scenario: VideoStream returns nullptr for audio-only file
- **WHEN** 文件不包含视频流
- **THEN** VideoStream() 返回 nullptr

#### Scenario: FormatContext is not publicly accessible
- **WHEN** 外部代码尝试访问 Demuxer 的 FormatContext
- **THEN** 编译失败（方法为 private 或已移除）

### Requirement: Decoder supports skip_frame during seek
DecoderNode SHALL 在 seek 追赶期设置 `codec_ctx_->skip_frame = AVDISCARD_NONREF` 加速软件解码,到达目标 PTS 后恢复 AVDISCARD_DEFAULT。硬件解码(`codec_ctx_->hw_device_ctx` 非空)SHALL NOT 设置 skip_frame:NONREF 与硬件解码器的组合尚未验证;硬解追赶仅靠 PTS 阈值丢帧(帧完整解码后丢弃,surface 正常归还)。

#### Scenario: 软件解码 seek 追赶跳帧
- **WHEN** 软件解码且 seek 后尚未到达目标 PTS
- **THEN** skip_frame 置 AVDISCARD_NONREF,到达目标后恢复 AVDISCARD_DEFAULT

#### Scenario: 硬件解码 seek 不跳帧
- **WHEN** 硬件解码(带 hw_device_ctx)时 seek
- **THEN** skip_frame 保持默认值,追赶期只按 PTS 阈值丢弃已解码帧

### Requirement: DemuxNode uses constructor injection for file path
DemuxNode SHALL 通过构造函数接收文件路径 `explicit DemuxNode(std::string file_path)`，移除对 NodeConfig 的依赖。

#### Scenario: DemuxNode constructed with path
- **WHEN** 以 `DemuxNode(std::string file_path)` 构造 DemuxNode
- **THEN** 文件路径经构造函数注入，不依赖 NodeConfig

### Requirement: DecoderNode self-configures via Negotiate
DecoderNode::Negotiate() SHALL 从 input_port_->Format().codec_params() 读取编码参数，缓存供 Prepare() 使用。移除 SetStream 和 stream_ 成员。Prepare() 使用缓存 codecpar 打开解码器。

#### Scenario: No more SetStream dependency
- **WHEN** 构建播放图
- **THEN** DecoderNode 仅需 AddNode + Connect + graph Negotiate/Prepare，无需手动 SetStream

### Requirement: DecoderNode queries HW device from graph
DecoderNode SHALL 移除 SetHWAccel 方法。帧域决策 SHALL 在 `Negotiate()` 完成:经 `graph_->GpuDevice()` 取设备,校验 `SupportsDecoder(codec)` 与下游端口 caps 是否接受设备域,满足才协商硬件域,否则软格式。

`Prepare()` SHALL 缓存图级设备引用,在打开解码器时挂载 `hw_device_ctx`(经 `av_buffer_ref`)、`get_format` 回调与 `opaque`。带硬件打开失败 SHALL 自动重试软件解码并记录日志。

#### Scenario: Hardware decode negotiated
- **WHEN** 图有 GPU 设备、编码器支持硬解、下游 caps 接受设备域
- **THEN** 输出端口协商为设备域,Prepare 打开带 hw_device_ctx 的解码器

#### Scenario: Downstream rejects hardware domain
- **WHEN** 下游 caps 不包含设备域(如渲染器后端不支持绑定)
- **THEN** 输出端口协商软格式,解码器不带硬件配置打开

#### Scenario: Hardware open fails, software retry
- **WHEN** 带硬件打开 `avcodec_open2` 失败
- **THEN** 释放该上下文,重试软件打开;再次失败才置 NodeState::kError

### Requirement: AudioSinkNode reads params from port format
AudioSinkNode SHALL 移除 SetStream 方法和 stream_ 成员，从 input_port_->Format() 读取 sample_rate 和 channels。

#### Scenario: AudioSinkNode configures from port format
- **WHEN** AudioSinkNode 配置音频输出
- **THEN** 从 input_port_->Format() 读取 sample_rate 和 channels，无需 SetStream

### Requirement: DecoderNode Negotiate 做格式推理
DecoderNode::Negotiate SHALL 从 EncodedFormat::codec_params 推理输出格式,不开 codec。Prepare SHALL 只剩资源分配。像素格式为占位,首帧后 SHALL 按实际 `AVFrame::format` 校正输出端口格式(含硬件帧的 hw_sw_format),格式变化时经 `OutputPort::SetFormat` 传播。

#### Scenario: Negotiate 算出输出格式不开 codec
- **WHEN** DecoderNode::Negotiate 执行
- **THEN** 从输入端口的 codec_params 构造输出 VideoFormat,未调用 avcodec_open2

#### Scenario: 运行时校正实际格式
- **WHEN** 解码出首个与实际格式不同于协商占位值的帧
- **THEN** 输出端口格式被更新为实际像素格式(硬件帧附带 hw_sw_format),后续同格式帧不再更新

### Requirement: 节点长函数提炼至 50 行内
DemuxNode/DecoderNode/VideoSinkNode/AudioSinkNode 的长函数 SHALL 提炼私有辅助方法，每个函数体不超过 50 行。DecodeLoop SHALL 不使用 goto。

#### Scenario: 节点函数体不超过 50 行
- **WHEN** 检查 DemuxNode/DecoderNode/VideoSinkNode/AudioSinkNode 的成员函数
- **THEN** 每个函数体不超过 50 行，DecodeLoop 中不出现 goto

### Requirement: 节点响应 OnCommand
DemuxNode/DecoderNode/AudioSinkNode SHALL 覆写 OnCommand 响应 kSeek：DemuxNode 重定位、DecoderNode 设 drop_until_pts、AudioSinkNode 清 SDL 缓冲。

#### Scenario: 节点自主响应 seek
- **WHEN** 各节点收到 OnCommand({kSeek, pos})
- **THEN** DemuxNode RequestSeek、DecoderNode SetDropUntilPts、AudioSinkNode FlushSdlBuffer

### Requirement: 硬件解码帧池由解码器按设备细化
DecoderNode 带硬件打开时 SHALL 设置 `extra_hw_frames`,为下游在途帧(链路深度 + sink 当前帧)预留解码 surface。`get_format` 选中设备硬件格式时 SHALL 自行创建帧池:`avcodec_get_hw_frames_parameters` 生成参数、补齐 FFmpeg 自动路径保证的工作 surface 数、调用 `GpuDevice::RefineDecoderFrames`、`av_hwframe_ctx_init`;任一步失败 SHALL 记录日志并退回 FFmpeg 默认帧池,不影响解码。解码器 SHALL 只依赖 `GpuDevice` 接口,不出现平台类型。

#### Scenario: 细化后的帧池可被 GPU pass 读取
- **WHEN** 硬件解码打开且设备支持对解码格式采样
- **THEN** 解码帧所在纹理带着色器资源绑定,GPU pass 直接读取其切片

#### Scenario: 细化失败回退默认帧池
- **WHEN** 自建帧池的 `av_hwframe_ctx_init` 失败
- **THEN** 记录告警,FFmpeg 以默认帧池继续硬件解码

### Requirement: 解码器不持有设备锁
DecoderNode SHALL NOT 在应用层为 `avcodec_send_packet` / `avcodec_receive_frame` / `avcodec_flush_buffers` 加设备锁:FFmpeg 通过交给它的递归锁在内部保护其命令上下文序列。DecoderNode SHALL 直接推送 FFmpeg 输出的原生硬件帧,不做呈现准备或下载。

#### Scenario: 解码不阻塞渲染
- **WHEN** 4K 关键帧在解码线程解析码流
- **THEN** 渲染线程只在 FFmpeg 真正提交 D3D11 命令的短区间内等待设备锁

#### Scenario: 推送原生硬件帧
- **WHEN** 解码出 D3D11 帧
- **THEN** 推送的 MediaFrame 即 FFmpeg 输出帧(数组纹理切片),呈现与下载由下游决定

