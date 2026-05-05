## Context

当前视频渲染链路：
```
Decoder(soft, avcodec_find_decoder)
  → AVFrame(CPU, YUV420P)
  → FrameQueue
  → VideoRenderer: SDL_UpdateYUVTexture (CPU→GPU 上传)
  → SDL_RenderTexture (GPU 合成输出)
```

SDL3 渲染器在 Windows 上默认选择 D3D11 后端，YUV→RGB 已由 GPU shader 完成。瓶颈在解码侧（软解 2K ~5ms/帧）和上传侧（SDL_UpdateYUVTexture 约 1-2ms/帧 for 4K）。

FFmpeg 的 D3D11VA hwaccel 输出帧格式为 `AV_PIX_FMT_D3D11`，`frame->data[0]` 是 `ID3D11Texture2D*`，`frame->data[1]` 是 texture array index。SDL3 的 D3D11 后端支持通过 `SDL_CreateTextureWithProperties` 直接绑定外部 D3D11 texture，实现零拷贝渲染。

## Goals / Non-Goals

**Goals:**
- 视频解码默认走 D3D11VA 硬解，每帧解码 <0.1ms
- 硬解输出的 D3D11 texture 零拷贝送入 SDL 渲染（无 GPU→CPU→GPU 往返）
- 硬解失败时静默回退到软解（对外 API 不变）
- Seek 优化（skip_frame + drop_until_pts）与硬解兼容

**Non-Goals:**
- 多平台硬解（VAAPI/VideoToolbox）——本次仅 Windows D3D11VA
- HDR / 10bit 色彩管理
- 替换 SDL（继续用 SDL3 做视频和音频）
- 用户可选硬解/软解的 UI 开关（本次自动探测）

## Decisions

### 1. 硬件加速类型选择 D3D11VA（而非 DXVA2 或 D3D12VA）

**选择**: `AV_HWDEVICE_TYPE_D3D11VA`

**理由**:
- SDL3 默认渲染后端是 D3D11，硬解输出与渲染在同一 D3D11 device 上可以零拷贝
- DXVA2 是 D3D9 时代 API，SDL3 不直接支持其 surface
- D3D12VA 更新但 SDL3 D3D12 后端尚不成熟，且 FFmpeg D3D12VA 支持也较新
- 覆盖 Windows 10+ 所有独显和核显

### 2. HWAccelContext 作为独立类（而非 Decoder 内部逻辑）

**选择**: 独立的 `HWAccelContext` 类，由 Player 层持有并注入 Decoder

**理由**:
- 单一职责：设备探测/创建/format 协商是独立关注点
- 可复用：音频未来若需硬件解码可共享同一 device
- 可测试：Mock HWAccelContext 测试 Decoder 回退逻辑
- 生命周期清晰：HWAccelContext 与 Player 同生命周期（不随单个 Decoder open/close 销毁）

### 3. get_format 回调由 HWAccelContext 提供（而非 Decoder 硬编码）

**选择**: `HWAccelContext::GetFormat` 静态回调 + 通过 `codec_ctx->opaque` 传递 context

**理由**:
- FFmpeg 在 `avcodec_open2` 后首次解码时调用 `get_format`，需要返回 `AV_PIX_FMT_D3D11` 表示接受硬件格式
- 回调需要访问 HWAccelContext 获取支持的 pixel format，通过 `opaque` 指针传入
- Decoder 不直接依赖具体硬件类型，保持通用性

### 4. VideoRenderer 内部分支渲染路径（而非 Strategy 继承）

**选择**: `VideoRenderer` 内部根据帧格式分支（`RenderHWFrame` / `RenderSWFrame`），不拆分为子类

**理由**:
- 当前只有一个 VideoRenderer 实例，SDL 后端固定
- 软硬帧的区别仅在 texture 创建方式（3 行代码），其余逻辑（dest rect、aspect ratio、present）完全一致
- 避免过度抽象——两个路径 90% 代码共享，拆子类增加不必要的 vtable 开销和复杂度
- 未来如需 OpenGL/Vulkan 后端再引入 Strategy

### 5. D3D11 texture 零拷贝方式：每帧创建临时 SDL_Texture（而非预分配复用）

**选择**: 每帧用 `SDL_CreateTextureWithProperties` 绑定硬解 texture，渲染后 `SDL_DestroyTexture`

**理由**:
- D3D11VA 解码输出通常是 texture array 的不同 slice（`frame->data[1]` 是 index），每帧 texture 不同
- SDL_CreateTextureWithProperties 只是包装一层（不复制数据），创建/销毁开销极小（~μs 级）
- 预分配 texture pool 需要知道 array size，与 FFmpeg 内部管理耦合
- MPV 和 VLC 均采用类似的"按帧绑定"策略

**备选方案**: 如果 profile 发现 SDL_CreateTexture 频繁调用有开销，可引入 SDL_Texture cache（LRU by texture pointer）

### 6. 回退策略：Open 时尝试 → 失败静默降级

**选择**: `HWAccelContext::Create` 失败返回 nullptr，Decoder::Open 检测到 nullptr 走原路径

**理由**:
- 一些老旧 GPU 或远程桌面环境不支持 D3D11VA
- 用户无感：日志记录 `"HW decode unavailable, falling back to software"`
- 不需要运行时切换——Open 时一次决定，整个播放周期不变

## Risks / Trade-offs

- **[兼容性] 部分视频 D3D11VA 不支持** → `get_format` 回调中如果 FFmpeg 不提供 `AV_PIX_FMT_D3D11`，返回软件格式 fallback。Decoder 检测首帧 `frame->format`，若非硬件格式则切回 SW 路径
- **[SDL texture 生命周期] frame unref 后 texture 失效** → 渲染必须在 frame 被 unref 前完成。当前 VideoRenderLoop 中 frame 在 Pop 后到下一次循环前都有效，满足条件
- **[D3D11 device 匹配] SDL renderer 与 FFmpeg 需共享同一 D3D11 device** → 从 SDL_Renderer 获取其内部 ID3D11Device，传给 FFmpeg 的 `av_hwdevice_ctx_create`。若 SDL 非 D3D11 后端则放弃硬解
- **[NV12 格式] D3D11VA 输出 NV12（非 YUV420P）** → SDL3 原生支持 `SDL_PIXELFORMAT_NV12` 纹理，无需格式转换
- **[Seek 兼容] skip_frame 在硬解下是否生效** → D3D11VA 支持 skip_frame hint（FFmpeg 文档确认），现有 seek 优化代码不受影响

## Migration Plan

1. 新增 `HWAccelContext` 类（独立文件，不影响现有代码）
2. 修改 `Decoder::Open` 增加可选参数（默认 nullptr = 原行为）
3. 修改 `VideoRenderer::Render` 增加硬件帧分支（原 SW 路径不变）
4. 修改 `PlayerImpl::Open` 尝试创建 HWAccelContext 并注入
5. 每步编译验证，SW 路径始终可用作回退
