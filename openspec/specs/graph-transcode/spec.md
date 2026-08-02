## Purpose

Defines the Transcoder class and MuxNode for transcode scenarios —
building transcode graphs that process media from source to encoded output.

## Requirements

### Requirement: Transcoder builds transcode graph
系统 SHALL 定义 `Transcoder` 类，作为转码场景的预设图构建器。

Transcoder SHALL 提供：
- `SetInput(const std::string& url)`
- `SetOutput(const std::string& path, const TranscodeOptions& options)`
- `Start()`：构建图并启动（无 Clock，全速处理）
- `Cancel()` / `SetProgressCallback(ProgressCallback)` / `SetCompletionCallback(std::function<void(bool ok)>)`

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
- **THEN** SetCompletionCallback 注册的回调被调用一次，参数为 true

#### Scenario: Completion callback reports failure on encode/mux error
- **WHEN** EncoderNode 或 MuxNode 在处理中报错（如 avcodec_send_frame 失败）
- **THEN** 错误通过 SPDLOG_ERROR 记录，节点转为 kError 状态，SetCompletionCallback 注册的回调被调用一次，参数为 false

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

### Requirement: 转码图各链路声明缓冲量
转码图的每一条连接 SHALL 显式声明容量。Decoder→Encoder 与 Encoder→Mux 两条链路 SHALL NOT 依赖任何隐式默认值。

Decoder→Encoder 的深度 SHALL 按"避免编码器饥饿"选取，而非沿用播放图的深度 —— 后者服务于 A/V 同步前瞻，转码无此需求。编码单帧的耗时远高于解码单帧，少量缓冲即足够；继续加深不带来收益，因为编码器自身持有的前瞻缓冲远大于链路缓冲。

#### Scenario: 转码不因缺失背压而耗尽内存
- **WHEN** 转码一个高分辨率长片，编码速度显著慢于解码
- **THEN** 解码线程被链路背压限速，峰值内存保持在与缓冲深度相称的量级，SHALL NOT 随片长增长

#### Scenario: 背压不降低转码吞吐
- **WHEN** 为 Decoder→Encoder 链路加上容量限制
- **THEN** 转码总耗时与输出内容不变 —— 编码器本就是瓶颈，解码器领先与否不影响完成时间
