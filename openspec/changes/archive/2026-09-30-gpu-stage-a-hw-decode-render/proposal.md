## Why

播放链路目前是纯软件管线:解码器打开时没有任何硬件加速配置(`decoder_node.cc` 的 `FindAndOpenCodec` 只做 `avcodec_find_decoder + avcodec_open2`),渲染器对所有帧做 CPU→GPU 上传。此前(2026-05)D3D11VA 硬解曾实现过,但因"硬解格式在 BuildGraph 之前拍板、绕开图协商"违反图架构而被整体删除(`0802e41`),`hw-accel` 规格里遗留的 `HWAccelContext` 设计正是那次反模式的编码。

本变更恢复硬解能力,但按图架构重新设计:帧域(硬件/软件)成为协商的一部分,设备成为图级共享资源,解码与渲染共享同一个 GPU 设备,渲染侧实现真正的零拷贝纹理绑定。

## What Changes

- 新增 `gpu` 层(`src/media/gpu/`):平台无关的 `GpuDevice` 接口 + D3D11 后端实现 + 工厂。它是代码库中唯一接触平台 GPU API 的地方,后续 VAAPI/VideoToolbox 后端以同一接口接入
- `MediaGraph` 增加共享资源 `SetGpuDevice/GpuDevice`(替换旧规格中未实现的 `SetHWDevice/HWDevice`),设备由管线编排器在 Open 之前注入
- 帧域进入协商:`PixelFormat` 增加硬件域枚举值(D3D11/CUDA/QSV/VAAPI/VideoToolbox,镜像 FFmpeg 的硬件 AVPixelFormat),`VideoFormat` 增加 `hw_sw_format` 字段;DecoderNode 在 Negotiate 读下游 caps("下游建议、上游决定")决定协商硬域还是软格式
- `DecoderNode`:Prepare 挂载 `hw_device_ctx` + `get_format` 回调;带硬件打开失败时自动重试软件解码;首帧后按实际格式校正输出端口格式
- `VideoSinkNode::DeclareCaps`:渲染器后端支持外部纹理绑定时接受硬件域,否则只接受软格式——协商自动把链路压回软解
- 特效节点(`TransformEffectNode`/`ColorEffectNode`):DeclareCaps 把下游约束中继到上游(格式透明节点);Process 在启用且参数非恒等时,把硬件帧在节点边界显式下载为软件帧再处理(恒等/禁用时零开销直通)
- `VideoRenderer`:请求并校验 D3D11 后端、提取渲染器设备供 FFmpeg 共享;`RenderHWFrame` 绑定解码线程生成的呈现纹理(独立纹理池,GPU blit 桥接数组纹理)实现零拷贝,渲染线程不触碰设备命令上下文
- `MediaPlayer::BuildGraph`:渲染器先 Open,其设备经 `GpuDevice::WrapExternal` 注入 graph

## Capabilities

### New Capabilities
- `gpu-device`:图级 GPU 设备抽象(GpuDevice 接口、平台后端、设备共享与生命周期契约、AV 像素格式与项目 PixelFormat 的双向映射)

### Modified Capabilities
- `hw-accel`:删除旧的 `HWAccelContext` 需求(设备构图前创建、硬编码 D3D11VA、解码专用),由 `gpu-device` + 协商期帧域决策取代
- `graph-shared-resources`:`SetHWDevice/HWDevice` 改为 `SetGpuDevice/GpuDevice`
- `demux-decode`:DecoderNode 在 Negotiate 按下游 caps 决定帧域,Prepare 挂接设备并带失败回退
- `video-renderer`:零拷贝路径从"声称支持"变为真实实现(外部纹理绑定),新增后端探测与绑定能力查询
- `media-frame`:PixelFormat 增加硬件域枚举值,MediaFrame 增加 IsHardware/HwSwFormat,新增 TransferToSoftware 域转换
- `port-format-negotiation`:VideoFormat 增加 `hw_sw_format`;帧域作为 pixel_formats 维度参与 caps 兼容性判断

## Impact

- 受影响代码:`src/media/gpu/`(新)、`media_frame.{h,cc}`、`graph/media_format.{h,cc}`、`graph/media_graph.{h,cc}`、`nodes/decoder_node.{h,cc}`、`nodes/video_sink_node.{h,cc}`、`nodes/transform_effect_node.cc`、`nodes/color_effect_node.cc`、`video_renderer.{h,cc}`、`media_player.cc`
- 不影响:音频链路(音频解码永远软件)、软件解码路径行为(回归冒烟已验证)、转码链路(本期不注入设备,保持全软)
- 风险:D3D11 帧池复用依赖解码器 surface 数量远大于在途帧数(与 mpv d3d11 实践一致);10-bit(P010)硬件帧在启用 CPU 特效时直通(日志警告一次);非 D3D11 的 SDL 后端整体回退软件路径
