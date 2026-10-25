## Purpose

Defines how end-of-stream travels through the pipeline — from the demuxer's
EOF through decoder draining to the sinks — and how the player turns it into
the finished state and callback.
## Requirements
### Requirement: Demuxer sends flush packet on EOF
Demuxer SHALL 在 `av_read_frame` 返回 `AVERROR_EOF` 时，向每个活跃的 PacketQueue 推入一个 flush packet（`data=NULL, size=0`），通知下游 Decoder 进入 drain 模式。

#### Scenario: EOF triggers flush packet
- **WHEN** Demuxer 的 `av_read_frame` 返回 AVERROR_EOF
- **THEN** Demuxer 向 audio PacketQueue 和 video PacketQueue 各推入一个 null packet（data=NULL, size=0）

#### Scenario: Flush packet carries current serial
- **WHEN** Demuxer 推入 flush packet
- **THEN** flush packet 携带当前的本地 serial 值

### Requirement: Decoder enters drain mode on null packet
Decoder SHALL 在从 PacketQueue 中 Pop 到一个 `data=NULL, size=0` 的 packet 时，调用 `avcodec_send_packet(ctx, NULL)` 进入 drain 模式，持续调用 `avcodec_receive_frame` 直到返回 AVERROR_EOF，将所有缓存帧输出到 FrameQueue。

#### Scenario: Null packet triggers drain
- **WHEN** Decoder Pop 到 data=NULL 的 packet
- **THEN** Decoder 调用 avcodec_send_packet(ctx, NULL) 并循环 receive 输出剩余帧

#### Scenario: Drain outputs all buffered frames
- **WHEN** Codec 内部缓存有 N 帧未输出
- **THEN** drain 后 FrameQueue 中新增 N 帧

### Requirement: Decoder pushes EOF marker after drain
Decoder SHALL 在 drain 完成（avcodec_receive_frame 返回 AVERROR_EOF）后，向 FrameQueue 推入一个 EOF 标记（frame=nullptr, eof=true）。

#### Scenario: EOF marker pushed after drain complete
- **WHEN** avcodec_receive_frame 在 drain 模式下返回 AVERROR_EOF
- **THEN** Decoder 向 FrameQueue Push 一个 eof=true 的标记

### Requirement: FrameQueue supports EOF marker
FrameQueue 的 SerialFrame 结构 SHALL 包含 `bool eof` 字段（默认 false）。Pop 时调用方 SHALL 检查 eof 字段。

#### Scenario: Normal frame has eof=false
- **WHEN** 正常 Push/Pop 一个 AVFrame
- **THEN** eof 字段为 false

#### Scenario: EOF marker has eof=true and frame=nullptr
- **WHEN** Pop 到 eof=true 的 SerialFrame
- **THEN** frame 为 nullptr，调用方知晓流已结束

### Requirement: Renderer notifies Player on EOF
VideoSinkNode 与 AudioSinkNode SHALL 在拉到当前世代的 EOS buffer、且之前的数据都已呈现/送入设备后，向图的消息总线投递 `kEos`（携带该世代 `serial`）。MediaPlayer 的事件线程 SHALL 按世代汇总：同一世代内所有活跃 sink 都报告后才转为 kFinished；旧世代的 EOS SHALL 被忽略。

#### Scenario: Video EOF alone does not finish (if audio exists)
- **WHEN** 视频流报告 EOF 但音频流仍在播放
- **THEN** 状态保持 Playing

#### Scenario: All streams EOF triggers Finished
- **WHEN** 所有活跃流（音频+视频，或仅视频）均报告 EOF
- **THEN** 状态转为 Finished

#### Scenario: EOS from before a seek is ignored
- **WHEN** 视频 sink 报告 EOS 后、音频 sink 报告之前用户 seek
- **THEN** 已收到的 EOS 作废，新位置播放到结尾时重新汇总

### Requirement: Player fires playback finished callback
MediaPlayer SHALL 提供 `SetPlaybackFinishedCallback(std::function<void()>)` 接口。每次播放到结尾（转为 kFinished）时 SHALL 调用该回调一次。回调 SHALL 在内部事件线程、facade 锁之外执行：其中可以调用 Play/Pause/Seek 与查询接口，`Open()`/`Close()` SHALL 被拒绝并记录错误日志（它们需要 join 事件线程本身）。

#### Scenario: Callback invoked on playback end
- **WHEN** 播放到文件末尾，状态转为 Finished
- **THEN** PlaybackFinished 回调被调用一次

#### Scenario: No callback if not registered
- **WHEN** 未注册回调，播放到文件末尾
- **THEN** 状态正常转为 Finished，无崩溃

#### Scenario: Close from the callback is refused
- **WHEN** 回调中调用 `Close()`
- **THEN** 调用被忽略并记录错误，事件线程不会 join 自身

### Requirement: Seek clears EOF state
Seek SHALL 清除 EOF 相关状态。Flush 操作清除队列中的 EOF marker。Demuxer seek 成功后恢复正常推包。如果当前状态为 Finished，Seek SHALL 将状态转为 Paused（暂停在目标位置）。

#### Scenario: Seek from Finished state
- **WHEN** 状态为 Finished，调用 Seek(10.0)
- **THEN** 状态转为 Paused，画面显示 10s 处的帧

#### Scenario: Flush removes EOF markers from queues
- **WHEN** 队列中有 EOF marker，执行 Flush()
- **THEN** EOF marker 被清除，serial 递增

