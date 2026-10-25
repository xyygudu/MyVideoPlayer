## Purpose

Defines the Transcoder class and MuxNode for transcode scenarios —
building transcode graphs that process media from source to encoded output.
## Requirements
### Requirement: Transcoder builds transcode graph
系统 SHALL 定义 `Transcoder` 类，作为转码场景的预设图构建器。

Transcoder SHALL 提供：
- `SetInput(const std::string& url)`
- `SetOutput(const std::string& path, const TranscodeOptions& options)`
- `Start()`：构建图并启动（无 Clock，全速处理）。已有运行中的转码时 SHALL 返回 false 且不上报；建图/协商/准备/启动失败时 SHALL 先以 `kFailed` 调用完成回调再返回 false
- `Cancel()`：提前结束运行中的转码，SHALL 在调用线程上以 `kCancelled` 上报；无运行中转码时为空操作
- `SetProgressCallback(ProgressCallback)` / `SetCompletionCallback(std::function<void(TranscodeResult)>)`

`TranscodeResult` SHALL 为 `kSucceeded`（输出完整写入、trailer 已写、文件已关闭）、`kCancelled`（已取消，部分输出已写 trailer）、`kFailed`（建图失败或运行期错误，详见日志）。每次启动的运行 SHALL 恰好上报一次；自然结束时回调在内部事件线程上执行，回调中可以销毁 Transcoder 或启动新一轮，但不得阻塞等待属主线程。析构 SHALL 停止运行中的转码且不调用回调。

`TranscodeOptions` SHALL 定义为：
```cpp
struct EncodeParams {
    std::string codec_name;                 // 空字符串 = 不输出该媒体类型
    RateControlMode rate_control{RateControlMode::kCrf};
    int crf{23};
    int64_t bitrate_bps{0};                 // rate_control == kBitrate 时生效
    int gop_size{250};
    int max_b_frames{2};
    std::string preset{"medium"};           // 仅对支持 preset 的编码器（如 libx264）生效
};

struct TranscodeOptions {
    EncodeParams video;
    EncodeParams audio;
};
```

Transcoder 内部图构建（v1 范围，不含 AVFilter/裁剪/直通拷贝/两遍编码/硬件编码）：
```
FileSource → Demux ─┬─► Decoder(V) → Encoder(V) ─┐
                    └─► Decoder(A) → Encoder(A) ──┤
                                                    └─► MuxNode → FileSink
```

v1 SHALL 仅支持单条视频流 + 单条音频流（或仅其一），`EncodeParams::codec_name` 为空表示该媒体类型不参与输出（既不创建 Decoder 也不创建 Encoder 分支）。

#### Scenario: Progress callback reports percentage
- **WHEN** 输入 60 秒文件，全速转码
- **THEN** ProgressCallback 被多次调用，百分比从 0 递增到 100

#### Scenario: Completion callback reports success
- **WHEN** 转码正常完成（所有输入达到 EOS，MuxNode 写完 trailer）
- **THEN** SetCompletionCallback 注册的回调被调用一次，参数为 kSucceeded

#### Scenario: Completion callback reports failure on encode/mux error
- **WHEN** EncoderNode 或 MuxNode 在处理中报错（如 avcodec_send_frame 失败）
- **THEN** 错误通过 SPDLOG_ERROR 记录，SetCompletionCallback 注册的回调被调用一次，参数为 kFailed

#### Scenario: Cancel reports cancelled and finalizes the file
- **WHEN** 转码进行中调用 Cancel()
- **THEN** 回调在 Cancel() 返回前以 kCancelled 被调用一次，输出文件已写 trailer、可被正常探测

#### Scenario: Second start while running is rejected
- **WHEN** 转码运行中再次调用 Start()
- **THEN** 返回 false，记录错误日志，不调用回调，原转码不受影响

#### Scenario: Missing input fails through the callback
- **WHEN** 输入文件不存在时调用 Start()
- **THEN** 回调以 kFailed 被调用一次，Start() 返回 false

#### Scenario: Video-only or audio-only output
- **WHEN** TranscodeOptions.audio.codec_name 为空字符串
- **THEN** Transcoder 不创建音频 Decoder/Encoder 分支，仅转码视频流

### Requirement: MuxNode multiplexes streams
系统 SHALL 定义 `MuxNode`（Sink 类型），使用 FFmpeg `avformat_write_header()` / `av_interleaved_write_frame()` / `av_write_trailer()`。

MuxNode SHALL 提供：
- N 个输入端口（每路输出媒体流一个），无输出端口
- ThreadingMode：Active——自身持有一个 fan-in 工作线程，从所有输入 Link 按 PTS 选取下一个待写入的包，避免多个上游 Encoder 线程并发调用同一个 AVFormatContext
- 容器格式由输出文件扩展名推断，不写死具体格式
- 容器需求声明：`DeclareCaps()` SHALL 用 `av_guess_format` 解析输出容器，按 `AVFMT_GLOBALHEADER` 将各输入端口 caps 的 `HeaderPlacement` 声明为 `kGlobal` 或 `kInBand`，供上游 Encoder 在协商期读取
- 编码器兼容性声明：`DeclareCaps()` SHALL 查询该容器支持的编码器集合（复用 `container-codec-caps` 提供的探测逻辑），将各输入端口 caps 的 `codec_ids` 声明为该媒体类型下容器支持的 codec_id 集合
- 可选的进度钩子：`SetProgressHook(std::function<void(double pts_seconds)>)`，每次成功写入主流（视频优先，否则音频）的包后调用

#### Scenario: Mux interleaves video and audio
- **WHEN** 视频和音频编码帧按 PTS 到达
- **THEN** av_interleaved_write_frame 按 PTS 排序写入

#### Scenario: Write trailer on stop
- **WHEN** 所有输入端口均收到 kEos
- **THEN** av_write_trailer() 被调用

#### Scenario: Single-threaded muxer access
- **WHEN** 视频 Encoder 线程和音频 Encoder 线程同时有数据到达 MuxNode 的输入 Link
- **THEN** MuxNode 自身的 fan-in 线程串行地从两个 Link 取数据并调用 av_interleaved_write_frame，不存在两个线程同时调用 FFmpeg muxer API 的情况

#### Scenario: MuxNode declares header placement in caps
- **WHEN** `DeclareCaps()` 解析输出为 matroska（含 `AVFMT_GLOBALHEADER`）
- **THEN** 各输入端口 caps 的 HeaderPlacement 为 kGlobal；解析为 mpegts 时为 kInBand

#### Scenario: Unknown container falls back to in-band headers
- **WHEN** `av_guess_format` 无法推断输出格式
- **THEN** 记录 WARN，HeaderPlacement 声明为 kInBand，不阻塞转码

#### Scenario: Progress hook reports current PTS
- **WHEN** MuxNode 成功写入一个主流包，其 PTS 为 12.5 秒
- **THEN** 已注册的 SetProgressHook 回调被调用一次，参数为 12.5

#### Scenario: MuxNode declares supported codecs in caps
- **WHEN** `DeclareCaps()` 解析输出容器为 mp4
- **THEN** 视频输入端口 caps 的 `codec_ids` 包含该容器支持的编码器（如 H.264），不支持的编码器不在集合内

### Requirement: 编码器与容器的兼容性在协商期校验
`EncoderNode::DeclareCaps()` SHALL 解析出编码器后，将其 codec_id 声明到输出端口 caps 的 `codec_ids`（单元素集合），媒体类型取自该编码器的 `AVMediaType`，不依赖尚未协商的输入格式。

编码器与容器的兼容性 SHALL 通过 `MediaGraph::Negotiate()` 已有的 `ValidateCaps()` 阶段校验——不引入额外的校验路径。不兼容组合 SHALL 在此阶段使协商失败，早于 `avcodec_open2` 与 `avformat_write_header`。

#### Scenario: 不兼容的编码器在协商期被拒绝
- **WHEN** EncoderNode 解析出的编码器不在下游 MuxNode 声明的 `codec_ids` 内
- **THEN** `MediaGraph::Negotiate()` 返回 false，`avcodec_open2`/`avformat_write_header` 均不会被调用

#### Scenario: 兼容的编码器正常协商通过
- **WHEN** EncoderNode 解析出的编码器在下游 MuxNode 声明的 `codec_ids` 内
- **THEN** `MediaGraph::Negotiate()` 正常返回 true，转码按原有流程继续

#### Scenario: 不确定的兼容性不阻塞协商
- **WHEN** MuxNode 对某编码器的支持性探测结果为"不确定"（`avformat_query_codec` 返回负值）
- **THEN** 该编码器被包含在 `codec_ids` 内，协商正常通过；真正的不兼容留给 `avformat_write_header` 的现有错误日志兜底

### Requirement: 转码图各链路声明缓冲量
转码图的每一条连接 SHALL 显式声明容量。Decoder→Encoder 与 Encoder→Mux 两条链路 SHALL NOT 依赖任何隐式默认值。

Decoder→Encoder 的深度 SHALL 按"避免编码器饥饿"选取，而非沿用播放图的深度 —— 后者服务于 A/V 同步前瞻，转码无此需求。编码单帧的耗时远高于解码单帧，少量缓冲即足够；继续加深不带来收益，因为编码器自身持有的前瞻缓冲远大于链路缓冲。

#### Scenario: 转码不因缺失背压而耗尽内存
- **WHEN** 转码一个高分辨率长片，编码速度显著慢于解码
- **THEN** 解码线程被链路背压限速，峰值内存保持在与缓冲深度相称的量级，SHALL NOT 随片长增长

#### Scenario: 背压不降低转码吞吐
- **WHEN** 为 Decoder→Encoder 链路加上容量限制
- **THEN** 转码总耗时与输出内容不变 —— 编码器本就是瓶颈，解码器领先与否不影响完成时间

### Requirement: 转码折叠时间戳跳变
Transcoder SHALL 以 `DemuxOptions{correct_ts_discontinuities = true}` 构造 DemuxNode，使带 `AVFMT_TS_DISCONT` 标志的容器（MPEG-TS 等）中的 DTS 跳变被折叠，输出时间轴连续（ffmpeg 命令行语义）。

#### Scenario: 两段起点相同的 TS 首尾相接
- **WHEN** 输入由两段各 3 秒、时间戳起点相同的 TS 直接拼接而成
- **THEN** 输出视频 DTS 严格递增、时长约 6 秒、约 150 帧，后一段接在前一段之后

### Requirement: 编码器输入时间戳严格递增
EncoderNode SHALL 丢弃 pts（换算到编码器时间基后）不大于上一帧的视频帧（ffmpeg `-fps_mode vfr` 的行为），首次丢弃记录告警，关闭时汇总丢弃数。音频 pts SHALL 由送入编码器的样本数推算，天然递增。

#### Scenario: 拼接点重复的时间戳
- **WHEN** 源在拼接点出现与上一帧相同的 pts
- **THEN** 该帧被丢弃，编码器不产生非法 DTS，muxer 不拒包

### Requirement: 编码器适配源格式中途变化
EncoderNode SHALL 按协商期确定的编码格式输出：源分辨率或像素格式中途变化时缩放/转换到编码器尺寸；音频经共享的 `AudioResampler` 跟随输入采样率、声道布局、样本格式的变化。编码器参数 SHALL 经 `avcodec_get_supported_config` 协商（如 Opus 只支持 48kHz 系列，44.1kHz 输入被重采样到 48kHz）。

#### Scenario: 后半段变为 320x240 / 48kHz 单声道
- **WHEN** 输入前 3 秒为 640x360 / 44.1kHz 立体声，后 3 秒为 320x240 / 48kHz 单声道
- **THEN** 转码成功，输出保持 640x360 / 44.1kHz 立体声，音视频时长均约 6 秒

#### Scenario: Opus 输出
- **WHEN** 以 libopus 编码 44.1kHz 音频
- **THEN** 转码成功，输出音频为 48kHz

