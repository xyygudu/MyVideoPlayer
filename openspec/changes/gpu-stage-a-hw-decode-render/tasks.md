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

## 5. 实机修复(首轮验收暴露:无画面)

- [x] 5.0 根因:解码(解码线程)与 transfer 回退(渲染线程)并发使用同一 D3D11 立即上下文;且解码器数组纹理无法被 SDL3 包装(非数组 SRV 限制)
- [x] 5.1 GpuDevice::CopyForPresentation:D3D11 后端实现独立纹理环池(8 个,SHADER_RESOURCE)+ CopySubresourceRegion blit,内部互斥锁
- [x] 5.2 MediaFrame 增加呈现纹理指针字段,move 语义携带
- [x] 5.3 DecoderNode:DrainFrames 在解码线程生成呈现纹理;失败时同线程下载软件帧;MaybeAnnounceFormat 增加音频守卫并按实际推送帧公告
- [x] 5.4 VideoRenderer:绑定呈现纹理;移除渲染线程上全部设备操作(RenderHwTransfer 删除,RenderFallback 拒绝硬件帧)
- [x] 5.5 ~MediaGraph 显式销毁顺序:节点先于 GPU 设备(池纹理生命周期)

## 5b. 实机修复(二轮验收:seek 不刷新 + 重开卡死)

- [x] 5b.1 根因:seek 追赶的 skip_frame=AVDISCARD_NONREF 与硬解不兼容——surface 池泄漏耗尽后 send_packet 永久阻塞,解码线程挂死(画面冻结 + Close join 挂起)
- [x] 5b.2 MaybeFlushOnSerialChange:仅软件解码设置 AVDISCARD_NONREF,硬解只靠 PTS 阈值丢帧
- [x] 5b.3 MediaPlayer::Close 增加分步调试日志,便于下次定位挂点

## 6. 验证

- [x] 6.1 构建通过(mvp_core.dll / mvp_app.exe / mvp_transcode_cli.exe)
- [x] 6.2 软件路径回归:ffmpeg 生成 2s 测试视频 → mvp_transcode_cli 转码成功、输出有效(解码/编码软路径不受影响)
- [x] 6.3 ffmpeg CLI 对照:本机 d3d11va 硬解 + 下载正常(机器级硬解能力确认)
- [x] 6.4 硬解播放有画面(用户确认)
- [ ] 6.5 seek 复测:多次 seek 后画面正常刷新、无卡顿
- [ ] 6.6 重开文件复测:关闭再打开新视频,无卡死(若复现,Close 分步日志可定位挂点)
- [ ] 6.7 特效开关验收:播放中启用色彩特效,画面生效;禁用后恢复零拷贝
- [ ] 6.8 `openspec validate gpu-stage-a-hw-decode-render --strict` 通过后 archive
