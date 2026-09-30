## MODIFIED Requirements

### Requirement: Render 接口使用 MediaFrame
VideoRenderer::Render SHALL 接受 `const MediaFrame&` 参数。

Render SHALL 通过 `frame.RawFrame()` 获取底层 `AVFrame*` 以访问帧数据。像素格式分发 SHALL 通过 `gpu::FromAvPixelFormat(frame.RawFrame()->format)` 完成（映射收口在 gpu 层，渲染器不维护自有映射表）。存在 GPU 设备时，Render SHALL 在整个渲染过程中持有设备锁（`gpu::ScopedContextLock`）。

渲染路径选择（按帧格式分支）：
1. 硬件帧 → `FramePresenter::BindableTexture` 取得可绑定纹理后零拷贝包装呈现；布局不可绑定时在锁内 `TransferToSoftware` 下载，按下列软件路径呈现；下载失败丢帧告警
2. `AV_PIX_FMT_NV12` → SDL_UpdateNVTexture
3. `AV_PIX_FMT_YUV420P` → SDL_UpdateYUVTexture
4. 其他格式 → sws_scale 转 YUV420P 后上传

#### Scenario: YUV420P 帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_YUV420P 的 MediaFrame
- **THEN** RenderYUV420P 被调用，通过 RawFrame()->data/linesize 读取数据

#### Scenario: 硬件帧零拷贝渲染
- **WHEN** Render 收到 D3D11 硬件帧且呈现器返回可绑定纹理
- **THEN** 经 `SDL_CreateTextureWithProperties(SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER)` 包装该纹理呈现，帧数据不发生 GPU→CPU 拷贝

#### Scenario: 不可绑定布局在锁内下载
- **WHEN** 硬件帧的 sw_format 非 NV12/P010
- **THEN** 渲染器在持设备锁时下载为软件帧并按软件路径呈现

### Requirement: 渲染器后端能力声明
VideoRenderer SHALL 在 Open() 时探测 SDL 渲染器后端；后端提供原生设备时 SHALL 调用 `GpuDevice::WrapExternal` 包装并以 `shared_ptr` 持有，同时创建 `FramePresenter`。渲染器 SHALL 暴露两项只读能力：`BindableHardwareDomain()`（设备包装成功时为其帧域，否则 kUnknown）与 `SharedGpuDevice()`（持有的设备，无则 nullptr）。二者 SHALL 仅由 Open/Close 修改，渲染线程与协商线程只读。Close() SHALL 先释放呈现器与设备引用，再销毁 SDL 渲染器。

由于设备会被解码线程共享，Open() SHALL 在创建渲染器前请求线程安全的后端设备（SDL3：`SDL_HINT_RENDER_DIRECT3D_THREADSAFE=1`；SDL 默认创建单线程 D3D11 设备）。

#### Scenario: 请求线程安全设备
- **WHEN** VideoRenderer::Open 创建 D3D11 渲染器
- **THEN** 创建前已设置 THREADSAFE hint，所得设备可开启多线程保护

#### Scenario: D3D11 后端探测
- **WHEN** SDL 渲染器后端为 D3D11 且设备包装成功
- **THEN** `BindableHardwareDomain()` 返回 kD3D11，`SharedGpuDevice()` 返回有效设备

#### Scenario: 非 D3D11 后端
- **WHEN** SDL 渲染器后端非 D3D11 或设备包装失败
- **THEN** 两查询分别返回 kUnknown 与 nullptr，VideoSinkNode 据此拒绝硬件域
