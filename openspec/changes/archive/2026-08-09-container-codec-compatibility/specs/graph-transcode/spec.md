## MODIFIED Requirements

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

## ADDED Requirements

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
