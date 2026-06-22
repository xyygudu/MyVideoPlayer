## 1. MediaFormat 扩展（纯新增，不破坏现有）

- [x] 1.1 在 `media_format.h` 新增 `#include <memory>` 和 FFmpeg codec_par include
- [x] 1.2 新增 `std::shared_ptr<AVCodecParameters> codec_params_` 私有成员
- [x] 1.3 新增 `const AVCodecParameters* codec_params() const` 访问器
- [x] 1.4 新增 `static MediaFormat FromStream(...)` 工厂方法声明
- [x] 1.5 在 `media_format.cc` 实现 FromStream：avcodec_parameters_alloc + copy + shared_ptr 包装，同时填充 width/height/sample_rate/channels 等字段
- [x] 1.6 编译验证（纯新增，不影响现有调用）

## 2. Port 格式传播

- [x] 2.1 在 `OutputPort::Connect()` 末尾新增 `peer->SetFormat(format_)` 调用
- [x] 2.2 编译验证

## 3. DemuxNode 输出端口携带完整参数

- [x] 3.1 DemuxNode::Prepare() 中将 `SetFormat(MediaFormat::Packet(...))` 改为 `SetFormat(MediaFormat::FromStream(...))`
- [x] 3.2 编译验证

## 4. DecoderNode 协商重构

- [x] 4.1 在 `decoder_node.h` 新增 `const AVCodecParameters* negotiated_codecpar_{nullptr}` 成员
- [x] 4.2 在 `decoder_node.h` 新增 `MediaGraph* graph_{nullptr}` 成员和 `void SetGraph(MediaGraph*)` 方法
- [x] 4.3 重写 `DecoderNode::Negotiate()`：从 input_port_->Format().codec_params() 读取参数并缓存
- [x] 4.4 重写 `DecoderNode::Prepare()`：使用 negotiated_codecpar_
- [x] 4.5 编译验证

## 5. HW 设备提升 Graph

- [x] 5.1 在 `media_graph.h` 新增 `shared_ptr<HWAccelContext> hw_device_` 成员和 SetHWDevice/HWDevice 接口
- [x] 5.2 DecoderNode::Prepare() 中从 graph_->HWDevice() 查询 HW 设备
- [x] 5.3 编译验证

## 6. MediaPlayer::BuildGraph 简化

- [x] 6.1 删除第二次 `avformat_open_input` 及 `avformat_close_input(&fmt_ctx)` 相关代码
- [x] 6.2 HW 设备改为 `graph_->SetHWDevice(HWAccelContext::Create(...))`
- [x] 6.3 DecoderNode 创建时调用 `SetGraph(graph_.get())`，不再调用 SetStream/SetHWAccel
- [x] 6.4 AudioSinkNode 从端口格式读取 sample_rate/channels（不再需要 SetStream）
- [x] 6.5 video_fps_ 改从 DemuxNode 输出端口 Format().frame_rate() 读取
- [x] 6.6 编译验证
- [ ] 6.7 运行验证（播放、seek、音视频同步）

## 7. 清理过渡代码 + 移除 NodeConfig

- [x] 7.1 DecoderNode 删除 `SetStream(AVStream*)` 方法和 `stream_` 成员
- [x] 7.2 DecoderNode 删除 `SetHWAccel(HWAccelContext*)` 方法
- [x] 7.3 AudioSinkNode 删除 `SetStream(AVStream*)` 方法和 `stream_` 成员
- [x] 7.4 DemuxNode 改为 `explicit DemuxNode(std::string file_path)` 构造注入
- [x] 7.5 VideoSinkNode 删除 Configure 实现
- [x] 7.6 AudioSinkNode 删除 Configure 实现
- [x] 7.7 从 `node.h` 删除 `struct NodeConfig` 和 `virtual bool Configure(const NodeConfig&) = 0`
- [x] 7.8 所有节点删除 Configure override
- [x] 7.9 各节点 Prepare 入口条件改为同时接受 kIdle/kConfigured
- [x] 7.10 编译验证（0 errors, 0 warnings）
- [ ] 7.11 运行验证（完整功能回归：播放、seek、音视频同步、HW加速）
