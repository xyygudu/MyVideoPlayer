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

## 5. 渲染器与线程契约

**设备操作单线程化是硬约束**(实测教训):D3D11 立即上下文不是线程安全的。第一版在渲染线程做 `av_hwframe_transfer_data` 回退,与解码线程的硬解提交并发使用同一命令上下文,直接导致无画面。修订后所有设备操作收敛到解码线程:

- **呈现纹理在解码线程生成**:`GpuDevice::CopyForPresentation(hw_frame)` 用 `CopySubresourceRegion` 把解码器数组纹理的当前子资源 blit 到设备拥有的独立纹理池(环形 8 个,D3D11_BIND_SHADER_RESOURCE)。同一命令上下文按提交顺序执行,blit 必然发生在解码写完之后——不需要额外 fence(mpv d3d11 interop 同款论证)。
- **为什么必须复制**:D3D11VA 解码器输出纹理数组(FFmpeg 头文件明示 "decoding requires a single array texture",`data[1]` 是子资源索引),SDL3 包装外部纹理后要建非数组 SRV,数组纹理必然失败。
- **下载回退也在解码线程**:CopyForPresentation 失败(不支持布局等)时,DecoderNode 直接 `av_hwframe_transfer_data` 下载为软件帧再推——与 ffmpeg CLI 单线程模型一致,已验证可行。
- **渲染线程与解码线程共享同一 immediate context**:D3D11 每个设备只有一个 immediate context,SDL 与 FFmpeg 拿到的是同一个。多调用序列(SDL 绘制+Present、FFmpeg 解码提交+blit)由 `GpuDevice::DeviceContextMutex()` 互斥(mpv ctx_lock 同款)。
- **共享设备必须线程安全**(实测根因,见 §5.1):设备级操作(纹理 Release、SRV 创建)只在非单线程设备上是线程安全的。
- **纹理指针运输**:MediaFrame 增加非拥有 `HwPresentationTexture()` 指针(池归设备所有),move 语义携带。环形池 8 > 在途帧数(链路 3 + current_frame 1),纹理被复用前所有引用早已呈现完毕——与解码器 surface 池同款余量论证。
- **析构顺序**:`~MediaGraph` 显式先清节点再释放设备,避免池纹理先于节点保留帧销毁。

Open() 请求 direct3d11 驱动,失败回退任意后端;探测 `SDL_PROP_RENDERER_D3D11_DEVICE_POINTER` 决定绑定能力,写入 `bindable_domain_`。RenderHWFrame 绑定 `HwPresentationTexture()` 逐帧呈现(NV12/P010 按 HwSwFormat 选格式);绑定失败或缺失纹理时丢帧并告警——绝不在渲染线程转换(会破坏单线程契约)。

### 5.1 共享设备的线程安全(seek 冻结的真实根因)

**现象**:4K 硬解播放中 seek,画面停在旧帧、声音约 1 秒后消失,关闭时 join 挂起。排查中曾先后误判为「skip_frame 导致 surface 泄漏」与「surface 池耗尽」,均被数据否定(surface 索引统计:全程只用到 20 个中的 9 个)。

**调用栈证据**:解码线程 `avcodec_send_packet → d3d11.dll → nvwgf2umx.dll → 等待`;渲染线程 `PresentFrame → current_frame_ = move(frame) → av_frame_free → avutil → d3d11.dll → nvwgf2umx.dll → 等待`。两线程在驱动内部互等。

**因果链**:
1. SDL3 默认以 `D3D11_CREATE_DEVICE_SINGLETHREADED` 创建设备(`SDL_HINT_RENDER_DIRECT3D_THREADSAFE` 默认 0),此时连 `ID3D10Multithread` 都拿不到,设备级调用也不是线程安全的。
2. h264 解码器 flush 后 `context_initialized=0`,下一个关键帧重走 `get_format`:FFmpeg 拆掉旧 hwaccel、放弃旧 hw_frames_ctx,新建解码器与 20 个 surface 的纹理数组(日志 `hw frames context replaced` 每次 seek 出现一次)。
3. 旧纹理数组随最后一个引用它的 AVFrame 释放而销毁——那是 VideoSink 的 `current_frame_`,在渲染线程、DeviceContextMutex 之外 Release;同一时刻解码线程正在同一设备上做 D3D11 视频解码调用 → 驱动死锁。

**决策**:`VideoRenderer::Open` 设置 `SDL_HINT_RENDER_DIRECT3D_THREADSAFE=1`;`D3D11GpuDevice::Wrap` 对被包装设备 `SetMultithreadProtected(TRUE)`,拿不到 `ID3D10Multithread`(单线程设备)时拒绝包装、整条管线回软解。应用层互斥锁无法覆盖「引用计数归零时在任意线程释放」的资源,只能交给 D3D11 运行时保护。
硬解追赶只按 PTS 阈值丢弃已完整解码的帧。原先「NONREF 导致 surface 泄漏、send_packet 永久阻塞」的论断已被 §5.1 否定(冻结根因是单线程设备),NONREF 与 D3D11VA 的组合尚未验证,作为 seek 提速候选记录在 docs/improvements/seek-performance.md
| | 设备来源 | 多线程保护 | 与解码共享的锁 |
|---|---|---|---|
| FFmpeg 自建设备 | `d3d11va_device_create` | 创建时开启 | 内部 `lock/unlock`(递归) |
| mpv d3d11 hwdec | 渲染器设备 | `SetMultithreadProtected(TRUE)` | FFmpeg 设备锁 |
| VLC d3d11 | 自建共享设备 | 创建时开启 | `context_mutex` 交给 FFmpeg |
| 本项目 | SDL 渲染器设备 | THREADSAFE hint + 包装时开启 | 应用层 DeviceContextMutex(粒度待收窄,见 docs/improvements/sink-threading-and-render.md) |

## 6. 回退矩阵(每处均有日志)

| 场景 | 行为 |
|---|---|
| 无 GPU / SDL 后端非 D3D11 / 包装失败 | 无设备 → 协商软格式 → 全软路径 |
| 编码器不支持硬解或带硬件打开失败 | open2 失败 → 重试软解(FFmpeg CLI 行为) |
| 帧布局非 NV12/P010,blit 无法执行 | 解码线程下载为软件帧后推送 |
| 呈现纹理缺失或 SDL 绑定失败 | 渲染器丢帧并告警(不跨线程转换) |
| 启用特效 + 硬件帧 | 节点边界下载处理;P010 等不支持格式直通并警告一次 |

## 7. 已知限制(记录在案)

- **硬解下 seek 追赶不用 skip_frame**:`AVDISCARD_NONREF` 会让 D3D11VA 输出被丢弃时泄漏 surface,池耗尽后 `avcodec_send_packet` 永久阻塞(实测:seek 后画面冻结 + 关闭时 join 挂起)。硬解追赶只按 PTS 阈值丢弃已完整解码的帧(surface 正常归还),追赶速度由 GPU 解码吞吐保证——与 mpv/ffplay 的 hwaccel 处理一致。
- **池复用的 GPU 采样余量**:呈现纹理池(8)与解码器 surface 池(FFmpeg 分配,通常 ≥8)都大于在途帧数(链路 3 + current_frame 1),最老纹理被复用前其呈现早已完成——与 mpv d3d11 的"靠池大小而非显式 fence"实践一致;若未来出现闪烁再引入 fence。
- **10-bit 硬件帧 + CPU 特效**:P010 下载后特效不处理(直通 + 一次性警告),渲染器可正常呈现。Stage B 解决。
- **单设备单硬解线程**:FFmpeg 的 D3D11VA 解码按单线程设计;转码双路硬解需评估设备锁或第二设备(Stage C 决策点)。
- **跨平台**:接口层跨平台,本期只有 D3D11 实现;VAAPI/VideoToolbox 以同一接口接入,Linux/macOS 实机验证留待后续。
