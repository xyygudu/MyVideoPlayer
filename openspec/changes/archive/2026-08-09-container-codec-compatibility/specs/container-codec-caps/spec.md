## Purpose

Defines a media-layer utility that, given an output container, reports
which video/audio encoders that container can store and which one is the
container's default — decoupled from FFmpeg types, reusable by both the
Transcoder UI (populating a codec dropdown) and MuxNode (declaring
`FormatCaps::codec_ids` during negotiation).

## ADDED Requirements

### Requirement: ContainerCodecCaps 结构体与查询函数
系统 SHALL 定义 `ContainerCodecCaps` 结构体，与 FFmpeg 类型解耦（编码器用 `std::string` 名字而非 `AVCodecID`），镜像 `SourceInfo` 的既定模式：

```cpp
struct ContainerCodecCaps {
    std::vector<std::string> video_codecs;
    std::vector<std::string> audio_codecs;
    std::string default_video_codec;   // 空字符串 = 该容器无默认视频编码器
    std::string default_audio_codec;   // 空字符串 = 该容器无默认音频编码器
};
```

系统 SHALL 提供查询函数，输入容器名或输出文件路径，返回该容器的 `ContainerCodecCaps`。

#### Scenario: mp4 容器的编码器集合
- **WHEN** 查询 "mp4" 容器
- **THEN** 返回的 `video_codecs` 包含 `"libx264"`，`audio_codecs` 包含 `"aac"`

#### Scenario: 无法推断的容器返回空集合
- **WHEN** 查询函数无法解析出 `AVOutputFormat`
- **THEN** 返回的 `ContainerCodecCaps` 各字段为空，不抛出异常、不崩溃

### Requirement: 候选编码器表使用精选列表而非穷举
查询函数 SHALL 对一个精选的候选编码器名字列表逐一探测，而不是穷举 FFmpeg 注册的全部编码器。候选名字若本地 FFmpeg build 未编译对应编码器，SHALL 静默跳过该候选，不视为错误。

#### Scenario: 未编译的候选编码器被跳过
- **WHEN** 候选列表包含 `"libx265"`，但本地 FFmpeg build 未编译该编码器
- **THEN** `avcodec_find_encoder_by_name("libx265")` 返回空，该候选不出现在结果中，函数正常返回其余候选

### Requirement: 容器支持性探测对不确定结果保持放行
查询函数 SHALL 使用 `avformat_query_codec()` 判断某候选编码器是否被容器支持。该函数对部分容器返回负值（信息不可用），此时 SHALL 视为"允许"而非"不支持"，避免把无法确认的组合误判为不兼容。只有返回值明确为 0 时才排除该候选。

#### Scenario: 无 codec_tag 表的容器不会误删候选
- **WHEN** 容器的 `avformat_query_codec()` 对某候选编码器返回负值
- **THEN** 该候选仍出现在 `ContainerCodecCaps` 的结果列表中

#### Scenario: 明确不支持的编码器被排除
- **WHEN** 容器的 `avformat_query_codec()` 对某候选编码器明确返回 0
- **THEN** 该候选不出现在结果列表中

### Requirement: 默认编码器取自容器注册的默认值
默认编码器 SHALL 取自 `AVOutputFormat::video_codec` / `audio_codec`（FFmpeg 为该容器注册的默认编码器），通过 `avcodec_find_encoder()` 解析为名字。若该默认编码器不在精选候选列表的探测结果中，SHALL 退化为结果列表中第一个候选。

#### Scenario: 容器默认编码器在候选列表中
- **WHEN** 容器的 `AVOutputFormat::video_codec` 解析后的名字出现在探测结果里
- **THEN** `default_video_codec` 为该名字

#### Scenario: 容器默认编码器不在候选列表中时退化
- **WHEN** 容器的默认编码器不在精选候选列表内
- **THEN** `default_video_codec` 取候选列表探测结果中的第一项；若结果为空则为空字符串
