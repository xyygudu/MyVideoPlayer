## Why

v1 转码（EncoderNode/MuxNode）对 matroska/mp4 输出会失败：编码器从未设置 `AV_CODEC_FLAG_GLOBAL_HEADER`，libx264 不生成 extradata，导致 `avformat_write_header` 返回 `AVERROR_INVALIDDATA`。修复该问题时又暴露出第二个潜在缺陷：音频编码器（AAC）要求每次 `avcodec_send_frame` 恰好 `frame_size` 个采样，而编码器直接喂入整帧重采样结果。两项现已修复；本变更将"global-header 由协商决定、Prepare 落地"的生命周期决策，以及音频 `frame_size` 缓冲行为固化进 spec。

## What Changes

- `MuxNode::Negotiate()` 用 `av_guess_format` 解析容器，把 `AVFMT_GLOBALHEADER` 结论（`needs_global_header`）发布到各输入端口；`EncoderNode::OpenCodec()`（在 `Prepare` 中、`avcodec_open2` 之前）通过输出端口 peer 读回该需求，仅在需要时设置 `AV_CODEC_FLAG_GLOBAL_HEADER`。
- `InputPort` 新增 `SetNeedsGlobalHeader/NeedsGlobalHeader` 需求通道，作为"下游需求向上游回流"的最小机制（caps 正式落地前的临时通道）。
- `EncoderNode` 用 `AVAudioFifo` 缓冲重采样音频，按 `codec_ctx_->frame_size` 凑满整帧再送编码器；EOF 时用静音补齐尾帧。
- `MuxNode` 的容器需求解析从构造函数移到 `Negotiate()`；`MuxNode` 构造函数恢复为纯 slot 搭建，`EncoderNode` 构造函数恢复为单一 `EncodeParams` 参数。
- `MuxNode::OpenOutput()` 失败时打印 `av_strerror` 具体错误码与各流 codecpar（原实现吞掉错误码）。

## Capabilities

### New Capabilities

（无新增 capability）

### Modified Capabilities

- `graph-transform-nodes`: EncoderNode 依据协商所得容器需求按需设置 `AV_CODEC_FLAG_GLOBAL_HEADER`；音频帧按 `frame_size` 缓冲后送入编码器。
- `graph-transcode`: MuxNode 在 `Negotiate()` 阶段解析容器的 global-header 需求并发布到输入端口。
- `port-format-negotiation`: InputPort 承载下游 `needs_global_header` 需求，协商期设置、Prepare 期消费。

## Impact

- 代码：`src/media/nodes/mux_node.{h,cc}`、`src/media/nodes/encoder_node.{h,cc}`、`src/media/graph/port.h`、`src/media/transcoder.cc`。
- 公共 API（`include/mvp/`）无变化，无 **BREAKING** 变更。
- 验证：mkv 输出 `needs_global_header=true`（extradata avcC 46B）、mpegts 输出 `needs_global_header=false`（SPS/PPS 在码流内），两者均完整转码成功。
