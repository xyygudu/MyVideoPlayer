## 1. 容器编码器查询工具

- [x] 1.1 新建 `include/mvp/container_caps.h`：定义 `ContainerCodecCaps` 结构体（`video_codecs`/`audio_codecs`/`default_video_codec`/`default_audio_codec`），不含任何 FFmpeg 类型
- [x] 1.2 新建 `include/mvp/container_probe.h` + `src/media/container_probe.cc`（镜像 `SourceProbe`/`source_probe.cc` 的头文件/实现分置约定）：实现查询函数——解析容器为 `AVOutputFormat`，对精选候选编码器表逐一 `avcodec_find_encoder_by_name` + `avformat_query_codec` 探测，负值按放行处理、0 才排除
- [x] 1.3 候选编码器表具名为常量（视频/音频各一个 `std::vector<std::string>`），注明"精选而非穷举"及追加方法
- [x] 1.4 默认编码器解析：`AVOutputFormat::video_codec`/`audio_codec` → `avcodec_find_encoder()` → 名字；不在候选探测结果内时退化为结果列表第一项
- [x] 1.5 确认新文件被 CMake 构建收录（`src/media` 用 GLOB_RECURSE，需要重新 `cmake --preset default` 后才会被 ninja 发现）

## 2. EncoderNode 声明编码器兼容性

- [x] 2.1 `EncoderNode::DeclareCaps()`：调用 `FindEncoder()` 解析编码器并存为成员，媒体类型取自 `codec_->type`（`AVMEDIA_TYPE_VIDEO`/`AUDIO`）映射到 `mvp::MediaType`
- [x] 2.2 输出端口 caps 设置为 `{media_type, codec_ids={codec_->id}}`
- [x] 2.3 `EncoderNode::Negotiate()` 中原有的 `FindEncoder()` 调用改为幂等（`codec_` 已解析则跳过），避免同一编码器解析两次
- [x] 2.4 确认 `Negotiate()` 里从输入 `Format()` 推断的 `media_type_` 与 `DeclareCaps()` 阶段从 `codec_->type` 推断的媒体类型一致，不产生矛盾

## 3. MuxNode 声明容器支持的编码器集合

- [x] 3.1 `MuxNode::DeclareCaps()` 在 `ResolveOutputRequirements()` 之后，调用容器编码器查询工具，按各 slot 的 `media_type` 取对应编码器名字列表
- [x] 3.2 把编码器名字转换为 `codec_id`（`avcodec_find_encoder_by_name(name)->id`），设置到该 slot 输入端口 caps 的 `codec_ids`
- [x] 3.3 补一条日志（SPDLOG_DEBUG 级别），记录容器为该 media_type 声明的 codec_ids 数量，便于协商失败时排查

## 4. UI 联动

- [x] 4.1 `TranscoderPage` 基础面板新增两个编码器下拉框（视频/音频），紧邻容器格式行
- [x] 4.2 容器下拉框 `currentIndexChanged` 时查询 `ContainerCodecCaps`，重建两个编码器下拉框的选项列表
- [x] 4.3 若当前选中编码器不在新容器支持列表内，重置为该容器的默认编码器；否则保留用户已选值
- [x] 4.4 `TranscoderPage` 首次构造时，用初始容器的 `ContainerCodecCaps` 初始化两个下拉框及默认选中项
- [x] 4.5 "开始转码"构造 `TranscodeOptions` 时，`codec_name` 从编码器下拉框当前选中值读取，移除硬编码的 `"libx264"`/`"aac"`

## 5. 验证

- [x] 5.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 5.2 现有 mp4/mkv + libx264/aac 组合转码回归：输出与改动前逐字节一致（沿用既有的转码回归验证方法）
- [x] 5.3 人为构造一个不兼容组合（如强制 `EncodeParams.video.codec_name` 为所选容器不支持的编码器），确认 `MediaGraph::Negotiate()` 在协商期返回 false，且日志能定位是哪条连接的 codec_ids 不兼容——而不是拖到 `avformat_write_header` 才报错
- [x] 5.4 UI 人工验收：切换容器下拉框，确认编码器下拉框选项与默认值正确联动，且此前选择的编码器若失效会被重置而非静默保留
- [x] 5.5 `openspec validate container-codec-compatibility --strict` 通过
