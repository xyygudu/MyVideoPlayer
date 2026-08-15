## 1. gpu 层(平台隔离)

- [x] 1.1 新建 `src/media/gpu/pixel_format_map.h`:AVPIX_FMT ↔ PixelFormat 双向映射唯一收口,取代 video_renderer.cc 里散落的 MapPixelFormat
- [x] 1.2 新建 `src/media/gpu/gpu_device.h`:GpuDevice 接口(Domain/DeviceRef/SupportsDecoder/WrapExternal),只前向声明 FFmpeg 类型
- [x] 1.3 新建 `src/media/gpu/d3d11_device.{h,cc}`:D3D11VA 后端,av_hwdevice_ctx_alloc + 外部设备注入 + av_hwdevice_ctx_init,SupportsDecoder 用 avcodec_get_hw_config 遍历
- [x] 1.4 新建 `src/media/gpu/gpu_device.cc` 工厂:平台注册点,Windows → D3D11 后端
- [x] 1.5 重新 `cmake --preset default` 使 GLOB 收录新文件,构建通过

## 2. 帧域协商

- [x] 2.1 `PixelFormat` 增加硬件域枚举值(kD3D11/kCuda/kQsv/kVAAPI/kVideoToolbox),media_frame.h
- [x] 2.2 `VideoFormat` 增加 `hw_sw_format` 字段,`MediaFormat::Video` 工厂增加参数(带默认值)
- [x] 2.3 `MediaFrame` 增加 `IsHardware()/HwSwFormat()`,新增 `TransferToSoftware()` 域转换
- [x] 2.4 DecoderNode::Negotiate 经 `PickOutputPixelFormat` 读下游 caps + GpuDevice::SupportsDecoder 决定帧域,不再硬编码 kYUV420P
- [x] 2.5 VideoSinkNode::DeclareCaps:按渲染器 BindableHardwareDomain 声明接受/拒绝硬件域
- [x] 2.6 特效节点 DeclareCaps:下游 caps 中继到输入端口(格式透明)

## 3. 设备注入与解码器

- [x] 3.1 `MediaGraph::SetGpuDevice/GpuDevice`(unique_ptr 持有,定义在 .cc 避免不完整类型)
- [x] 3.2 `MediaPlayer::Open` 渲染器提前到 BuildGraph 之前打开;BuildGraph 注入 WrapExternal 的设备
- [x] 3.3 DecoderNode::Prepare 缓存 graph 设备;TryOpenCodec 支持挂 hw_device_ctx/get_format/opaque
- [x] 3.4 带硬件打开失败自动重试软件解码,日志明确
- [x] 3.5 DrainFrames 首帧后 MaybeAnnounceFormat 校正输出端口格式(含 hw_sw_format)

## 4. 特效与渲染

- [x] 4.1 TransformEffectNode::Process:恒等直通 → 硬帧下载 → 处理(函数 ≤50 行)
- [x] 4.2 ColorEffectNode::Process:同上;不支持格式时硬件帧保持原样直通,软帧转发已下载副本
- [x] 4.3 VideoRenderer::Open:请求 direct3d11 驱动 + 后端探测 + 提取设备指针
- [x] 4.4 RenderHWFrame 拆分:RenderBoundHwFrame(外部纹理绑定零拷贝)+ RenderHwTransfer(原回退路径)
- [x] 4.5 Present 增加纹理参数重载

## 5. 验证

- [x] 5.1 构建通过(mvp_core.dll / mvp_app.exe / mvp_transcode_cli.exe)
- [x] 5.2 软件路径回归:ffmpeg 生成 2s 测试视频 → mvp_transcode_cli 转码成功、输出有效(解码/编码软路径不受影响)
- [ ] 5.3 硬解实机验收(需用户 GUI 操作):播放 H.264/HEVC 视频,确认日志 "hardware decode enabled" + "VideoRenderer … hw binding on",任务管理器 GPU Video Decode 有占用
- [ ] 5.4 特效开关验收:播放中启用色彩特效,画面生效(确认边界下载路径);禁用后恢复零拷贝
- [ ] 5.5 `openspec validate gpu-stage-a-hw-decode-render --strict` 通过后 archive
