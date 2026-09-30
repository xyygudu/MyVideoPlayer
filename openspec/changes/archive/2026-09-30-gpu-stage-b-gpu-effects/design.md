## Context

Stage A（`archive/2026-09-30-gpu-stage-a-hw-decode-render`）建立了：`gpu` 层 `GpuDevice` 接口 + D3D11 后端、帧域参与协商、解码与 SDL 渲染共享同一 `ID3D11Device`、解码线程把数组纹理 blit 到 8 张环形纹理供 SDL 绑定。其 design §5 的前提"设备操作必须收敛到解码线程（立即上下文非线程安全）"在 §5.1 根因修复（开启 `SetMultithreadProtected`）后已经不成立。

本变更实现 Stage B：特效在 GPU 上运行（自写 HLSL），并以此为契机修正 Stage A 的锁、所有权、呈现池与职责边界（见 proposal 的四个问题）。

已核实的外部事实（源码/头文件）：

- FFmpeg 7.1 `AVD3D11VADeviceContext.lock/unlock/lock_ctx`：未设置时 `d3d11va_device_init` 才创建内部 mutex；文档要求"锁必须递归"。`ff_dxva2_lock` 在 `DecoderBeginFrame…DecoderEndFrame` 与解码器创建外加锁，`d3d11va_transfer_data` 亦加锁。
- FFmpeg `AVHWDeviceContext.free/user_opaque`：在设备上下文最后一次 unref、`device_uninit` 之后调用。
- FFmpeg `hwcontext_d3d11va`：`initial_pool_size == 0` 或 `BindFlags` 含 `RENDER_TARGET` 时逐帧分配独立纹理（`d3d11va_alloc_single`），帧经 `AVBufferPool` 引用计数复用。解码帧池尺寸按 16（H.264）/128（HEVC/AV1）对齐，纹理大于可见尺寸。
- SDL3 3.2.16 D3D11 渲染器：输入布局、顶点着色器、VS 常量缓冲只在 `D3D11_CreateDeviceResources` 设置一次；`InvalidateCachedState` 只重置 RTV/光栅化/混合/像素着色器/SRV/采样器/视口/裁剪；外部纹理 SRV 固定为 `TEXTURE2D` 维度（不能绑定数组切片），并按 `(w+1)&~1` 推算 NV12 纹理尺寸。
- SDL3 3.2 的 D3D11 渲染器不接受外部设备（创建属性只有 Vulkan 可注入设备），因此"SDL 创建、FFmpeg 共享"仍是唯一选项。

## Goals / Non-Goals

**Goals:**
- 亮度/对比度/饱和度与仿射变换对硬件帧在 GPU 上执行，结果与 CPU 公式一致（NV12 与 P010）
- 单一递归设备锁，粒度为单次 GPU 操作；解码器不再持锁
- 硬件纹理生命期全部由引用计数管理，删除一切"池够大"的隐式不变量
- 呈现关注点归渲染器，解码器只产出原生硬件帧
- 与 SDL 共享立即上下文时不破坏 SDL 的管线状态

**Non-Goals:**
- 图自动插入 upload/download/convert 节点（见决策 6）
- 转码链路注入设备、硬件编码（Stage C）
- 渲染器跨文件存活、SRV/RTV 视图缓存、跨 seek 复用解码帧池（均为性能项，记入改进点）
- 非 D3D11 后端

## Decisions

### 1. 设备由输出侧共享持有

`VideoRenderer::Open` 在探测到 D3D11 后端后调用 `GpuDevice::WrapExternal`，以 `std::shared_ptr` 持有；`MediaGraph::SetGpuDevice(shared_ptr)` 共享持有，节点仍取裸指针（graph 生命期内有效）。锁、呈现器都跟随设备或渲染器，不再有"渲染器持有 graph 资源裸指针"的倒置。

| | 设备归属 | 解码器如何拿到 |
|---|---|---|
| mpv | VO（`ra_d3d11`），跨文件存活 | `d3d11_wrap_device_ref` 引用 |
| VLC 4 | `vlc_decoder_device`（引用计数，vout 窗口侧创建） | decoder / display 各持引用 |
| GStreamer | `GstD3D11Device`（`new_wrapped` 可包装外部设备），经 GstContext 在元素间共享 | 元素持引用 |
| 本项目（Stage B） | `VideoRenderer` 包装并 `shared_ptr` 持有，每文件重建 | graph 共享持有，解码器 `av_buffer_ref(DeviceRef())` |

备选：MediaPlayer 持有设备 —— 设备来自渲染器后端，放在渲染器里依赖更短，且与 mpv/VLC 一致，故不取。

### 2. 单一递归锁交给 FFmpeg，锁随 FFmpeg 设备上下文生灭

`Wrap` 在 `av_hwdevice_ctx_init` **之前**分配 `std::recursive_mutex`，设置 `hwctx->lock/unlock/lock_ctx`，并用 `AVHWDeviceContext.free + user_opaque` 在设备上下文销毁时释放它。应用侧（GPU pass、呈现器拷贝、SDL 绘制）通过 `GpuDevice::LockContext/UnlockContext`（RAII：`gpu::ScopedContextLock`）使用同一把锁；FFmpeg 解码与下载在内部加同一把锁。`DecoderNode` 删除全部加锁代码。

| | 多线程保护 | 应用侧锁 | 与 FFmpeg 的关系 |
|---|---|---|---|
| mpv | `SetMultithreadProtected` | 无（所有非解码 GPU 工作都在 VO 线程） | 用 FFmpeg 内部锁 |
| VLC | `SetMultithreadProtected` | `context_mutex` | 经 `hw.context_mutex` 交给 FFmpeg，显示端共用 |
| GStreamer | — | `gst_d3d11_device_lock`（`std::recursive_mutex`） | 自研解码器，共用该锁 |
| 本项目 Stage A | 开启 | `std::mutex` 包住整个 send/receive | 两把锁互不知情 |
| 本项目 Stage B | 开启 | 递归锁，单次 GPU 操作粒度 | 交给 FFmpeg，全局唯一 |

不选 mpv 式"无应用锁"：本项目特效是图节点，在解码线程执行"设状态 + Draw"的多调用序列，与渲染线程的 SDL 序列会交错，运行时多线程保护只保证单次调用原子。

锁的生命期绑定 FFmpeg 设备上下文而非 `GpuDevice` 对象：帧、帧池、解码器都经 `AVBufferRef` 持有设备上下文，可能晚于 `GpuDevice` 包装对象释放，锁必须活得和 FFmpeg 一样久。

### 3. 纹理生命期全部交给 FFmpeg 帧池

- **解码帧池**：`get_format` 选中硬件格式时，`avcodec_get_hw_frames_parameters` 生成参数 → `initial_pool_size += 3`（补齐 FFmpeg 自动路径保证的 4 个工作 surface）→ `GpuDevice::RefineDecoderFrames` 加 `D3D11_BIND_SHADER_RESOURCE`（格式支持采样时）→ `av_hwframe_ctx_init`。失败则退回 FFmpeg 默认帧池。`extra_hw_frames = 6` 为下游在途帧（链路 3 + sink 当前帧）预留 surface（mpv `hwdec-extra-frames` 默认值）。解码器仍只知道 `GpuDevice` 接口，D3D11 细节在后端。
- **pass 输出**：pass 自有 `AVHWFramesContext`（`BindFlags = RENDER_TARGET | SHADER_RESOURCE`，`initial_pool_size = 0`，尺寸对齐到 2），FFmpeg 逐帧分配独立纹理并引用计数复用。输出纹理既能作为下一个 pass 的输入，也能被 SDL 直接绑定。
- 删除 Stage A 的 8 张环形呈现池与 `MediaFrame::HwPresentationTexture`。

### 4. 呈现适配器归渲染器（mpv 1-copy）

`GpuDevice::CreatePresenter()` 返回 `FramePresenter`，由 `VideoRenderer` 持有。`BindableTexture(frame)`：纹理是独立纹理、带 SRV 绑定、尺寸等于对齐到 2 的可见尺寸时直接返回帧自身纹理（GPU 特效输出走这里，零拷贝）；否则按可见区域 `CopySubresourceRegion` 到呈现器自有纹理（解码器数组纹理走这里，与 mpv 默认 `d3d11va-zero-copy=no` 相同）。在渲染线程、设备锁内执行，拷贝与随后的绘制在同一上下文按序执行。

不可绑定布局（非 NV12/P010）在渲染线程锁内 `TransferToSoftware` 后走软件上传——多线程保护与统一锁使这不再违反线程约束。

### 5. GPU pass：构建期编译的 HLSL，保存/恢复共享上下文状态

- **接口**（`gpu/video_pass.h`）：`VideoKernel { kColorAdjust, kAffineRemap }`；每个内核的 uniform 结构体布局与 HLSL cbuffer 一致（16 字节对齐，`static_assert`）；`VideoPass::Run(src, uniforms, size)` 返回新硬件帧。节点只依赖接口，HLSL 与 D3D11 代码只在 gpu 层。
- **执行**：每个平面一次全屏三角形 Draw（`SV_VertexID` 生成，无顶点缓冲/输入布局）。NV12/P010 的 Y 平面用 `R8/R16_UNORM` RTV，UV 平面用 `R8G8/R16G16_UNORM` RTV；源用 `TEXTURE2DARRAY` 维度 SRV 读取数组切片（独立纹理视作 ArraySize=1 的数组）。源纹理无 SRV 绑定时先拷贝到 pass 的暂存纹理。
- **uniform 分两级**：b0 是 pass 常量（可见平面尺寸、源纹理平面尺寸、中性灰值），由 pass 按平面与位深填写；b1 是内核参数，由节点填写。颜色内核用 `Load` 逐像素读取（与 CPU LUT 公式逐项一致），仿射内核按 CPU 同款逆映射在像素空间计算、双线性采样，越界填黑（Y=0、UV=中性灰）。
- **编译**：`fxc` 构建期编译为字节码头文件（SDL/GStreamer 的做法），着色器错误在构建期暴露；`CMakeLists` 找不到 `fxc` 时配置期报错。SM 4.0（`vs_4_0/ps_4_0`）。
- **状态隔离**：pass 在锁内保存它会修改的管线状态（IA 布局/拓扑、VS、PS 与其 CB 0–1/SRV 0–1/采样器 0、光栅化、视口、混合、深度模板、RTV/DSV），执行后恢复（Dear ImGui `imgui_impl_dx11` 的备份/恢复模式）。仅靠 `SDL_FlushRenderer` 不够：SDL 的输入布局和顶点着色器只设置一次，失效后不会重设。

备选：运行期 `D3DCompile`（mpv/GStreamer 回退路径）—— 错误只能在运行时发现，放弃；`ID3D11DeviceContext1::SwapDeviceContextState` —— 与视频上下文的交互文档不清，放弃。

### 6. 双实现 + 按帧域分派，暂不做图自动插入转换节点

特效节点对硬件帧走 GPU pass，软件帧走 CPU，GPU pass 不可用（无设备、格式不支持 RTV、单帧失败）时回退为节点边界下载（Stage A 行为）。因此特效链路"硬件进硬件出、软件进软件出"，域不因特效开关而改变，不需要 GStreamer 式 upload/download 节点。DeclareCaps 维持 Stage A 的 caps 中继。当出现仅 CPU 或仅 GPU 的特效时，再引入自动插入转换节点。

## Risks / Trade-offs

- [NV12 RTV 需要硬件支持 `D3D11_FORMAT_SUPPORT_RENDER_TARGET`] → 设备包装时检查；不支持时 `CreateVideoPass` 返回 nullptr，节点回退下载路径
- [解码纹理加 SRV 绑定可能被个别驱动拒绝] → `av_hwframe_ctx_init` 失败时退回 FFmpeg 默认帧池，pass 对无 SRV 的源先拷贝到暂存纹理
- [每帧创建 SRV/RTV 与 SDL 包装纹理] → 驱动调用微秒级，4K60 下可接受；视图缓存记入改进点
- [设备锁在渲染线程覆盖整个 Present] → SDL 默认 `DXGI_PRESENT_DO_NOT_WAIT`（syncInterval 0），Present 不阻塞
- [解码 surface 在链路中被持有] → `extra_hw_frames = 6`；GPU 特效开启时 pass 输出新纹理，解码 surface 立即归还
- [HLSL 与 CPU 公式的细微差异] → 颜色内核逐项同公式（中性灰 128/255）；仿射越界改为硬截断（CPU 为双线性混合到填充色），边缘 1 像素差异可接受

## Migration Plan

纯内部重构 + 新增 GPU 路径，无外部 API 变化。回滚即还原提交。构建新增 `fxc` 依赖（Windows SDK 自带）。

## Open Questions

- 渲染器每文件重建导致设备每文件重建；是否让渲染器跨文件存活（mpv VO 模式）留待后续变更决定。
