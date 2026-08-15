# Design: GPU Stage A — 硬解 + 零拷贝渲染

## 1. 为什么不是"恢复旧代码"

2026-05 的 D3D11VA 实现被 `0802e41` 删除,原因记录在提交信息里:"硬件解码格式在 BuildGraph 之前决定,违反图架构"。旧规格 `hw-accel/spec.md` 里的 `HWAccelContext` 有三个问题,本设计逐一回避:

1. **时机反模式**:旧规格要求 `MediaPlayer::BuildGraph()` 直接 `SetHWDevice(HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA))`——设备与格式在图协商之前拍板。本设计:设备注入只提供"能力"(存在与否),帧域是否采用完全由 `DeclareCaps → ValidateCaps → Negotiate` 决定。
2. **上帝类**:`HWAccelContext` 混合了设备生命周期、get_format 回调、像素格式枚举三个职责,且硬编码 D3D11VA。本设计:设备生命周期归 `GpuDevice`(gpu 层),get_format 是 `DecoderNode` 的私有静态函数(解码关注点),格式枚举在 `PixelFormat`(已有)。
3. **零拷贝不可能成立**:旧设计自建设备,与 SDL 渲染器设备不是同一个,解码纹理无法直接呈现。本设计:设备从渲染器后端提取(`SDL_PROP_RENDERER_D3D11_DEVICE_POINTER`),FFmpeg 与 SDL 共享同一 `ID3D11Device`。

## 2. 帧域协商(核心决策)

参考 GStreamer caps negotiation 与 FFmpeg 硬件 AVPixelFormat 模型:

- **帧域是 PixelFormat 的取值**:`kD3D11/kCuda/kQsv/…` 镜像 FFmpeg 硬件 AVPixelFormat,精确到设备域。不采用泛化的"kHardware"——Stage C 的 nvenc 需要"kCuda 域"才能协商 D3D11→CUDA 映射,泛化域表达不了这个约束。
- **下游建议、上游决定**(项目既有模式,EncoderNode 读 HeaderPlacement 同款):DecoderNode::Negotiate 读 `output_port_->Peer()->Caps()`,下游 caps 接受设备域且 `GpuDevice::SupportsDecoder(codec)` 为真,才协商硬件域;否则软格式。
- **caps 中继**:特效节点是格式透明节点(禁用时直通),其 DeclareCaps 把输出端口下游的 caps 复制到输入端口——Sink 的"不支持硬件绑定"约束因此能穿过特效链到达解码器,协商整体回软。这避免了"特效默认接入图就永远软解"的误伤。
- **运行时校正**:协商值是占位,首帧后 `MaybeAnnounceFormat` 用真实 `AVFrame::format` 校正输出端口格式(含 hw_sw_format),与既有 `kFormatChanged` 事件语义一致。

## 3. 设备共享与生命周期契约

- 渲染器先 Open(设备存在)→ `MediaPlayer` 取 `NativeDevice()` → `GpuDevice::WrapExternal` → `graph->SetGpuDevice` → Open/Negotiate/Prepare。
- `WrapExternal` 用 `av_hwdevice_ctx_alloc + hwctx->device = 外部设备 + av_hwdevice_ctx_init` 包装,FFmpeg 按其文档接管接口引用。
- **析构顺序契约**:FFmpeg 释放 AVHWDeviceContext 时会 Release 设备接口,因此 graph 必须先于渲染器析构。`MediaPlayer::Close()` 已有 `graph_.reset()` → `video_renderer_.Close()` 的顺序,注入点注释固化该契约。
- 设备不可用(无 D3D11 后端、包装失败)时 graph 内为 null,协商自然走软件——注入永远不阻塞构图。

## 4. 特效节点:显式域转换边界

CPU 特效只能处理系统内存平面。启用的特效遇到硬件帧时,在节点边界调用 `TransferToSoftware`(av_hwframe_transfer_data)显式下载,处理后再发。恒等参数/禁用时零开销直通硬件帧——所以默认播放(特效参数恒等)完全不受影响。这是临时边界:Stage B 的 GPU 引擎将取代下载路径,节点接口(IEffectNode)不变。

## 5. 渲染器

- `Open()` 请求 direct3d11 驱动,失败则回退 SDL 任意后端;探测 `SDL_PROP_RENDERER_D3D11_DEVICE_POINTER` 确定绑定能力,存入 `bindable_domain_`。
- `RenderHWFrame`:D3D11 帧 + 可绑定 → `SDL_CreateTextureWithProperties(SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER)` 包装解码纹理逐帧呈现(帧生命周期由 VideoSinkNode::current_frame_ 保证);否则走原 transfer 路径(现为回退分支)。
- NV12/P010 按 `MediaFrame::HwSwFormat()` 选 SDL 纹理格式;未知布局走 transfer 兜底。

## 6. 回退矩阵(每处均有日志)

| 场景 | 行为 |
|---|---|
| 无 GPU / SDL 后端非 D3D11 / 包装失败 | 无设备 → 协商软格式 → 全软路径 |
| 编码器不支持硬解或带硬件打开失败 | open2 失败 → 重试软解(FFmpeg CLI 行为) |
| 硬帧纹理绑定失败 / 未知 sw 布局 | transfer 上传回退 |
| 启用特效 + 硬件帧 | 节点边界下载处理;P010 等不支持格式直通并警告一次 |

## 7. 已知限制(记录在案)

- **帧池复用与 GPU 采样**:解码器 surface 池(FFmpeg 分配,通常 ≥8)远大于在途帧数(Link 深度 3 + current_frame_),最老在途 surface 被复用时其呈现早已完成。与 mpv d3d11 的"靠池大小而非显式 fence"实践一致;若未来出现闪烁再引入 fence。
- **10-bit 硬件帧 + CPU 特效**:P010 下载后特效不处理(直通 + 一次性警告),渲染器可正常呈现。Stage B 解决。
- **跨平台**:接口层跨平台,本期只有 D3D11 实现;VAAPI/VideoToolbox 以同一接口接入,Linux/macOS 实机验证留待后续。
