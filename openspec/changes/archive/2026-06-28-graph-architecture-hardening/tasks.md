## 1. MediaFormat variant 重构（先做，影响面最广）

- [x] 1.1 在 `media_format.h` 定义 EncodedFormat/VideoFormat/AudioFormat 结构体
- [x] 1.2 MediaFormat 改为 `std::variant<monostate, EncodedFormat, VideoFormat, AudioFormat>` payload + 公共 media_type/time_base
- [x] 1.3 新增 IsVideo/IsAudio/IsEncoded + AsVideo/AsAudio/AsEncoded 类型安全访问；删除扁平 width()/sample_rate() 等访问器
- [x] 1.4 更新工厂 Video()/Audio()/FromStream()（FromStream 产出 EncodedFormat）
- [x] 1.5 FormatCaps::Intersect 用泛型 IntersectVectors<T> 重构，收缩至 50 行内
- [x] 1.6 grep 全部 `fmt.width()/height()/sample_rate()/channels()/codec_params()` 调用点，迁移到 AsVideo/AsAudio/AsEncoded
- [x] 1.7 编译验证 + 运行验证（播放正常）

## 2. Negotiate/Prepare 职责厘清

- [x] 2.1 DecoderNode::Negotiate 从 codec_params 推理输出格式，SetFormat 到输出端口（不开 codec）
- [x] 2.2 DecoderNode::Prepare 删除输出格式设置，只剩 codec 查找/打开/HW 配置
- [x] 2.3 编译验证 + 运行验证

## 3. 长函数提炼 + 消除 goto（纯重构）

- [x] 3.1 DecoderNode::DecodeLoop 提炼 MaybeFlushOnSerialChange/ProcessPacket/HandleEos，消除 goto
- [x] 3.2 DecoderNode::Prepare 提炼 FindAndOpenCodec/ConfigureHWAccel
- [x] 3.3 DemuxNode::Prepare 提炼 OpenFile/FindStreams/CreateOutputPorts
- [x] 3.4 DemuxNode::DemuxLoop 提炼 HandlePendingSeek/EmitEos/RoutePacket
- [x] 3.5 VideoSinkNode::RenderLoop 提炼 HoldLastFrameUntilStop/SyncAndRender
- [x] 3.6 VideoSinkNode::ComputeDisplayDelay 拆 ComputeAudioMasterDelay/ComputeVideoMasterDelay
- [x] 3.7 AudioSinkNode::AudioLoop 提炼 ShouldThrottle/ConvertAndFeed/DrainAndReportEos
- [x] 3.8 AudioSinkNode::Prepare 提炼 ReadAudioParams/OpenSdlDevice
- [x] 3.9 编译验证（所有函数体 ≤ 50 行）

## 4. 源探测形式化

- [x] 4.1 在 `node.h` 定义 StreamInfo 结构体 + ISourceNode（含 Probe 纯虚）
- [x] 4.2 DemuxNode 继承 ISourceNode，实现 Probe（打开文件 + 返回 StreamInfo 含 duration）
- [x] 4.3 DemuxNode::Prepare 改幂等（OpenFile 判 format_ctx_ 空守卫）
- [x] 4.4 编译验证

## 5. PlaybackGraphBuilder

- [x] 5.1 新增 `nodes/playback_graph_builder.h/.cc`，定义 PlaybackContext + FilterSpec
- [x] 5.2 实现 AddVideoPipeline/AddAudioPipeline（链式，filters 默认空）+ 内部 ConnectChain
- [x] 5.3 MediaPlayer::BuildGraph 重构：Probe → builder 逐流 AddPipeline → graph Negotiate/Prepare（从 104 行收缩）
- [x] 5.4 MediaPlayer 缓存 duration（来自 Probe），Duration() 读缓存
- [x] 5.5 编译验证

## 6. 事件化控制

- [x] 6.1 新增 `graph/graph_command.h`，定义 CommandType{kSeek} + Command
- [x] 6.2 INode 新增 `virtual void OnCommand(const Command&) {}` + `virtual void SetPaused(bool)` 默认空实现
- [x] 6.3 MediaGraph 新增 Seek(double)（Flush + SendCommand）/SetPaused(bool)/SendCommand
- [x] 6.4 DemuxNode::OnCommand 响应 kSeek（RequestSeek）
- [x] 6.5 DecoderNode::OnCommand 响应 kSeek（SetDropUntilPts）
- [x] 6.6 AudioSinkNode::OnCommand 响应 kSeek（FlushSdlBuffer）；SetPaused override
- [x] 6.7 MediaPlayer::Impl 删除 demux_node_/video_decoder_/audio_decoder_/video_sink_/audio_sink_ 成员
- [x] 6.8 MediaPlayer::Seek 改为 graph_->Seek(t) + clock reset；Pause/Play 改为 graph_->SetPaused
- [x] 6.9 CurrentPosition 读 has_audio_ 标志决定用 audio_clock 还是 video_clock；Duration 读缓存
- [x] 6.10 编译验证

## 7. 最终验证

- [x] 7.1 全量构建（0 error / 0 warning）
- [x] 7.2 运行回归：播放、seek（播放中 + 暂停后）、暂停/恢复、音视频同步、HW 加速
- [x] 7.3 确认无函数超 50 行、无 goto、MediaPlayer 不持有单个节点
- [x] 7.4 重新生成 VS 工程（build-vs 已更新）
