## Context

`EncoderNode` 解析编码器只依赖 `EncodeParams::codec_name`（构造期配置），`MuxNode` 解析容器只依赖输出文件扩展名（`av_guess_format`），两者互不感知，唯一交界点是 `avformat_write_header()` 失败时的错误日志。

`graph/media_format.h` 里的 `FormatCaps` 已经有 `codec_ids` 字段，`FormatCaps::Compatible()`/`Intersect()` 也已经在处理它（`Disjoint(a.codec_ids, b.codec_ids)`），这是 `graph-lifecycle-and-caps-negotiation` 变更为跨节点协商建的通用机制，`MuxNode::DeclareCaps()` 已经在用同一机制声明 `HeaderPlacement`。但 `codec_ids` 从未被任何节点真正填充——协商框架具备能力，只是没有生产者。

输入侧已有 `SourceProbe`/`SourceInfo`（`source-probe`/`source-info` spec）作为"探测媒体信息、与 FFmpeg 类型解耦、供 UI 使用"的既定模式，本次在输出侧需要一个对称的工具。

## Goals / Non-Goals

**Goals:**
- 复用已有的 `FormatCaps.codec_ids` + `MediaGraph::Negotiate()` 两遍协商机制，让编码器/容器不兼容在 `avcodec_open2`/`avformat_write_header` 之前、`ValidateCaps()` 阶段就失败——不引入新的校验路径
- 新增一个媒体层查询工具，给定容器，返回其对视频/音频各支持的编码器集合与默认编码器，供 UI 和图构建两侧共用同一个判断（同一个"某容器支持某编码器"的事实只算一次）
- UI 编码器下拉框随容器下拉框联动，默认值取容器默认编码器，替换掉现在硬编码的 `libx264`/`aac`

**Non-Goals:**
- 不新增编码能力——只是把已经能通过 `avcodec_find_encoder_by_name` 解析到的编码器暴露/校验出来，不为此新增任何编解码器
- 不做穷举式"枚举 FFmpeg 支持的全部编码器"，只维护一个精选候选列表（后续按需追加即可，一行改动）
- 不涉及两遍编码、硬件编码、trim/passthrough——沿用 `transcoder-ui` spec 已声明的 v1 范围
- 不改变 `MediaGraph::Negotiate()` 四步协商流程本身

## Decisions

### D1: 复用 FormatCaps.codec_ids + 既有 ValidateCaps，不新建校验路径
备选方案：在 `Transcoder::SetOutput()` 里手写一次前置校验（构图前先查一遍容器是否支持所选编码器）。
拒绝理由：这会制造第二套"编码器是否兼容容器"的判断逻辑，和协商期的 `FormatCaps` 校验各自维护、容易出现"UI 侧放行、协商期又拒绝"或反过来的不一致；且 CLI 工具（`mvp_transcode_cli`）不走 UI 层，若校验只放在 `SetOutput()` 前置检查里，CLI 仍需要重复实现一遍。协商期校验对 UI 和 CLI 一视同仁，是唯一需要维护的地方。

### D2: EncoderNode 在 DeclareCaps() 里解析一次编码器，媒体类型取自 codec_->type
`DeclareCaps()` 按逆拓扑序先于所有节点的 `Negotiate()` 执行，此时 `input_port_->Format()` 尚未被上游写入（上游的 `Negotiate()` 还没跑），因此不能像现在的 `Negotiate()` 那样从输入格式推断 `media_type_`。但 `avcodec_find_encoder_by_name(params_.codec_name)` 本身只依赖构造期参数，可以提前到 `DeclareCaps()` 执行；媒体类型改用 `codec_->type`（`AVMEDIA_TYPE_VIDEO`/`AVMEDIA_TYPE_AUDIO`）映射到 `mvp::MediaType`，不依赖输入格式。
`DeclareCaps()` 解析出的 `codec_` 直接存为成员，`Negotiate()` 里原有的 `FindEncoder()` 调用改为"若尚未解析则解析"，避免同一件事做两遍。

### D3: MuxNode 用精选候选列表 + avformat_query_codec 探测容器支持的编码器
`avformat_query_codec(ofmt, codec_id, std_compliance)` 需要逐个 codec_id 试探，没有"列出容器支持的全部编码器"的直接 API；穷举 `av_codec_iterate()` 返回的全部 codec（数百个）逐一探测既昂贵又多数无意义（本项目目前只有 libx264/aac 经过端到端验证）。
改为维护一个精选候选表：
```cpp
// 视频候选（按常见程度排序）
{"libx264", "libx265", "libvpx-vp9"}
// 音频候选
{"aac", "libopus", "libmp3lame"}
```
对每个候选名调用 `avcodec_find_encoder_by_name()`（本地 FFmpeg build 未必都编译了这些编码器，解析失败的候选直接跳过，不报错），再用 `avformat_query_codec(ofmt, resolved_id, FF_COMPLIANCE_NORMAL)` 过滤。追加新候选是在这个数组里加一个字符串，不涉及架构改动。

### D4: avformat_query_codec 返回负值（不确定）时按"放行"处理
FFmpeg 头文件原话："@return 1 if codec with ID codec_id can be stored in ofmt, 0 if it cannot. A negative number if this information is not available." 不少不带 `codec_tag` 表的 muxer 对任意 codec 都返回负值。
若把"不确定"当"不支持"处理，会把本来合法的组合从候选列表和 `codec_ids` 声明里误删；因此只有**明确返回 0** 才排除该候选，负值视为"允许，交给运行期兜底"。
后果：对这类容器，`ValidateCaps()` 不能 100% 拦下所有不兼容组合——`MuxNode::OpenOutput()` 现有的 `avformat_write_header` 失败详情日志（codec_id/extradata_size/w/h/sr/ch）仍是这类边缘情况的最终兜底，本次改动不承诺覆盖它们，只是缩小失败面并提前大多数真实场景的报错时机。

### D5: 容器默认编码器取 AVOutputFormat::video_codec/audio_codec
`avformat_alloc_output_context2()` 拿到的 `AVOutputFormat` 自带 `video_codec`/`audio_codec` 字段（FFmpeg 注册的该容器默认编码器），用 `avcodec_find_encoder()`（按 ID 而非按名字）解析出对应的编码器名字，作为 UI 下拉框的默认选中项。若该默认编码器不在本项目的精选候选表里（可能发生在冷门容器上），退化为候选表中第一个能解析且通过 `avformat_query_codec` 的候选。

### D6: 新增查询工具，镜像 SourceProbe/SourceInfo 的既有模式
新文件（暂定 `src/media/container_probe.{h,cc}` + `include/mvp/container_caps.h`），输出纯数据结构，不泄漏 FFmpeg 类型：
```cpp
struct ContainerCodecCaps {
    std::vector<std::string> video_codecs;  // 编码器名字，如 "libx264"
    std::vector<std::string> audio_codecs;
    std::string default_video_codec;
    std::string default_audio_codec;
};
```
供 `TranscoderPage` 直接使用，也可被 `MuxNode::DeclareCaps()` 内部复用同一套探测逻辑（避免 UI 和图两处各写一份候选表+探测代码）。

### D7: UI 侧容器切换时联动刷新编码器下拉框
`TranscoderPage` 监听 `container_combo_` 变化，查询新容器的 `ContainerCodecCaps`，重建编码器下拉框选项；若用户此前选中的编码器不在新容器的支持列表里，重置为新容器的默认编码器（不允许静默保留一个已失效的选择）。

## Risks / Trade-offs

- [Risk] `avformat_query_codec` 对部分容器返回"不确定"，无法 100% 拦下不兼容组合 → 缓解：D4 的放行策略 + 保留现有 `avformat_write_header` 失败详情日志作为最终兜底，本设计不假装解决了这类边缘情况
- [Risk] 精选候选表可能不含用户想用的编码器（完整性有限） → 缓解：不是正确性问题，只是 UI 暂不提供该选项；追加候选是一行改动
- [Risk] 本地 FFmpeg build 未必编译了 libx265/libvpx-vp9/libopus/libmp3lame → 缓解：D3 已处理——`avcodec_find_encoder_by_name` 解析失败的候选直接跳过，不影响 libx264/aac 的既有行为
- [Trade-off] `transcoder-ui` 现有 spec 明确写"不暴露编码器选择"，本次改动收回这条约束——不是破坏性变更（无持久化配置需要迁移），但需要在 UI 交互测试里重新确认基础面板/高级面板的信息密度是否失衡

## Open Questions

- 精选候选表的具体编码器集合，需要在实现时用本地 FFmpeg build 实测哪些能通过 `avcodec_find_encoder_by_name` 解析成功，再最终确定
- 编码器下拉框放在 `TranscoderPage` 的基础面板还是高级面板，取决于放入后基础面板的信息密度是否需要调整布局
