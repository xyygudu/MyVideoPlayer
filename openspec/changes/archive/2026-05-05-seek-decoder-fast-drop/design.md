## Context

当前 Decoder 解码流程：seek 后 Demuxer 从最近关键帧发包 → Decoder flush 后逐帧解码 → 所有帧入 FrameQueue → VideoRenderLoop 根据 `seek_target_` 丢弃早帧。

问题：2K H.264 GOP=60 的流，关键帧到目标帧之间最多 59 帧都被软件完整解码，仅仅为了被 render loop 丢弃。Profiling 显示 78% CPU 在 `avcodec-61.dll`。

业界参考：
- **MPV** `mp_decoder_wrapper.c`: 设置 `hrseek_framedrop` + `start_pts`，decoder wrapper 在帧出来后判断 pts < start_pts 直接丢弃，不送 VO；同时设置 `skip_frame = AVDISCARD_NONREF` 跳过非参考 B 帧的解码。
- **FFplay** `video_thread`: 类似，通过 `framedrop` 配合 master clock 跳帧。
- **VLC** `decoder.c`: `DecoderFixTs` + preroll 标记，preroll 帧不送 output。

本项目选择最贴近 MPV 的方案：skip_frame + decoder-level drop。

## Goals / Non-Goals

**Goals:**
- 将 seek-to-display 延迟降低 50%+（2K H.264 soft decode 场景）
- 不改变最终显示帧的正确性（帧精确 seek 结果不变）
- 保持 render loop `seek_target_` 兜底机制不删除

**Non-Goals:**
- 硬件解码加速（D3D11VA/VAAPI）——独立 change
- Seek request coalescing（连续拖拽去重）——独立 change
- 音频 seek 优化（音频帧小，解码快，不是瓶颈）

## Decisions

### 1. skip_frame = AVDISCARD_NONREF（而非 AVDISCARD_NONINTRA）

**选择**: seek 期间设 `AVDISCARD_NONREF`

**理由**: 
- `AVDISCARD_NONREF` 跳过不被其他帧引用的帧（典型为 B 帧），解码器仍保留参考帧链，解码结果正确。
- `AVDISCARD_NONINTRA` 更激进（只保留 I 帧），但会破坏参考链导致花屏或需要额外 flush。
- MPV 实测验证该级别最安全。

**恢复时机**: 当解码出的帧 pts ≥ drop_until_pts_ 时，恢复 `AVDISCARD_DEFAULT`。

### 2. Decoder 内部丢帧（而非依赖 FrameQueue 满阻塞）

**选择**: Decoder `DecodeLoop` 内判断 `frame->pts < drop_until_pts_` 则 `av_frame_unref` 不入队。

**理由**:
- FrameQueue 容量有限（当前 4 帧），如果解码帧全部入队会导致队列满阻塞，render loop 取帧丢弃才能腾空间——形成串行瓶颈。
- Decoder 内部丢弃可以全速解码不被队列限制，latency 最小。
- 与 MPV `mp_decoder_wrapper` 的 `start_pts` 机制一致。

### 3. 接口设计：SetDropUntilPts(double pts) + ClearDrop()

**选择**: 通过 atomic<double> `drop_until_pts_` 控制，Player 层在 Seek 时设定。

**理由**:
- Decoder 线程持续运行，不阻塞不加锁；Player 主线程 atomic store，Decoder 线程 atomic load。
- `ClearDrop()` 在到达目标帧时由 Decoder 自动调用（self-clearing），不需要 Player 回调。
- 简单、无竞态。

### 4. PTS 比较使用 stream time_base 换算后的 double

**选择**: `drop_until_pts_` 使用秒为单位的 double（与 FrameQueue 中 frame pts 一致）。

**理由**:
- 项目内 `frame->pts * av_q2d(stream_->time_base)` 已经是标准换算路径。
- 避免引入额外的 time_base 传递复杂度。

## Risks / Trade-offs

- **[精度] skip_frame 可能跳过紧邻目标帧的 B 帧** → Render loop `seek_target_` 兜底保证最终帧正确；最差情况显示前一个参考帧（相距 1-2 帧，用户感知不到）。
- **[兼容性] 部分 codec 不支持 skip_frame** → FFmpeg 文档标注 "codec-dependent hint"，MPEG-4/H.264/H.265 均支持。对于不支持的 codec 设了也无副作用（被忽略）。
- **[线程安全] codec_ctx->skip_frame 由两个线程写** → 不会：SetDropUntilPts 只设 atomic double；skip_frame 由 DecodeLoop 自己在每次 serial change 时根据 drop_until_pts_ 值设定，单线程写。
- **[回归] 帧精确度测试** → 需增加单元测试验证 seek 前后帧 pts 匹配目标。
