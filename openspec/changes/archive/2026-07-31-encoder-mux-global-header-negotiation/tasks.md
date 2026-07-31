## 1. Global-header 协商

- [x] 1.1 `InputPort` 增加 `SetNeedsGlobalHeader(bool)` / `NeedsGlobalHeader()` 字段与方法
- [x] 1.2 `MuxNode::Negotiate()` 用 `av_guess_format` 解析容器，`ResolveOutputRequirements()` 由构造函数移至 Negotiate，并把 `needs_global_header` 写入各输入端口
- [x] 1.3 `EncoderNode::OpenCodec()` 在 `avcodec_open2` 前读取 `output_port_->Peer()->NeedsGlobalHeader()`，按需设置 `AV_CODEC_FLAG_GLOBAL_HEADER`
- [x] 1.4 移除 facade 注入：`Transcoder::WireBranch` 不再传 `mux->NeedsGlobalHeader()`；`EncoderNode` 构造函数恢复为单一 `EncodeParams`；删除 `MuxNode::NeedsGlobalHeader()` 公共接口

## 2. 音频 frame_size 缓冲

- [x] 2.1 `EncoderNode` 用 `AVAudioFifo` 缓冲重采样音频，按 `codec_ctx_->frame_size` 凑满整帧送入 `avcodec_send_frame`（`ProcessAudioFrame`/`SendCompleteAudioFrames`）
- [x] 2.2 EOF 时 `FlushAudioFifo` 用静音补齐尾帧后冲刷编码器（`HandleEos`）
- [x] 2.3 `CloseCodec` 释放 FIFO

## 3. 诊断与注释

- [x] 3.1 `MuxNode::OpenOutput()` 失败时打印 `av_strerror` 错误码与各流 codecpar
- [x] 3.2 精简本次新增的大段注释与类级注释

## 4. 验证

- [x] 4.1 重新构建通过（`cmake --build build`）
- [x] 4.2 mkv 输出：`needs_global_header=true`，完整转码成功，ffprobe 可见 h264 extradata（avcC）
- [x] 4.3 mpegts 输出：`needs_global_header=false`，完整转码成功，ffprobe 可解析（SPS/PPS 在码流内）
