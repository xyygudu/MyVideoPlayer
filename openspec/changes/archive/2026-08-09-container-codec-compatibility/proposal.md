## Why

转码时编码器与容器格式目前互不感知：`EncoderNode` 只根据 `EncodeParams::codec_name` 解析编码器，`MuxNode` 只根据输出文件扩展名推断容器，两者从不核对编码器是否被容器支持。不兼容组合（如把 HEVC 塞进只认 H.264 的容器）要拖到 `avformat_write_header()` 才报错，此时解码/编码资源已经分配。UI 侧同理：容器下拉框和编码器选择互相独立，用户可以选出一个从一开始就注定失败的组合。

`FormatCaps` 已经有 `codec_ids` 维度且 `FormatCaps::Compatible()` 已经在比较它（`graph-lifecycle-and-caps-negotiation` 变更引入），但 `EncoderNode`/`MuxNode` 都没有在 `DeclareCaps()` 里填充这个字段——协商框架具备能力，只是没人接上这条边。

## What Changes

- `EncoderNode::DeclareCaps()`：解析编码器后，把 `codec_->id` 声明到输出端口的 `FormatCaps::codec_ids`
- `MuxNode::DeclareCaps()`：用 `avformat_query_codec()` 查询输出容器实际支持的编码器集合，声明到各输入端口的 `FormatCaps::codec_ids`；对没有 `codec_tag` 表、返回值不确定（`AVERROR_PATCHWELCOME`）的容器保持放行，不误判为不兼容
- 新增一个媒体层查询工具（对称于既有的 `SourceProbe`/`SourceInfo`），给定容器名/输出路径，返回该容器对视频/音频各支持哪些编码器、以及容器的默认编码器，与 FFmpeg 类型解耦
- `TranscoderPage` 新增编码器选择（视频/音频各一个下拉框），随容器下拉框变化而刷新可选项与默认值；不再硬编码 `codec_name = "libx264"/"aac"`
- 不引入新的校验机制——复用已有的 `MediaGraph::Negotiate()` 两遍协商（`DeclareCaps → ValidateCaps → Negotiate`），不兼容组合在资源分配前的 `ValidateCaps()` 阶段就失败

## Capabilities

### New Capabilities
- `container-codec-caps`：查询"某容器对某媒体类型支持哪些编码器、默认编码器是什么"的媒体层工具，供 UI 与图构建两侧复用

### Modified Capabilities
- `port-format-negotiation`：`FormatCaps` 新增"编码器兼容性"维度的显式需求（`codec_ids` 字段已存在于代码但从未被文档化或真正填充过）
- `graph-transcode`：`EncoderNode`/`MuxNode` 的 `DeclareCaps()` 增加编码器兼容性声明，协商失败的时机从 `avformat_write_header()` 提前到 `MediaGraph::Negotiate()`
- `transcoder-ui`：编码器不再固定为 `libx264`/`aac`，改为按所选容器过滤的下拉框，默认取容器的默认编码器

## Impact

- 受影响代码：`src/media/nodes/encoder_node.{h,cc}`、`src/media/nodes/mux_node.{h,cc}`、`src/media/graph/media_format.{h,cc}`（如需要新增枚举/校验辅助）、新文件（容器编码器查询工具）、`src/app/transcoder_page.cc`
- 不影响：`MediaGraph` 协商流程本身（复用现有 4 步协商）、播放链路（改动仅限转码相关节点）
- 风险：`avformat_query_codec` 对部分容器返回"不确定"而非明确的支持/不支持，需要设计如何处理这一档
