## MODIFIED Requirements

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

## ADDED Requirements

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
