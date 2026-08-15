## MODIFIED Requirements

### Requirement: Render 接口使用 MediaFrame
VideoRenderer::Render SHALL 接受 `const MediaFrame&` 参数。

Render SHALL 通过 `frame.RawFrame()` 获取底层 `AVFrame*` 以访问帧数据。像素格式分发 SHALL 通过 `gpu::FromAvPixelFormat(frame.RawFrame()->format)` 完成（映射收口在 gpu 层，渲染器不维护自有映射表）。

渲染路径选择（按帧格式分支）：
1. `PixelFormat::kD3D11` → 绑定帧携带的呈现纹理零拷贝呈现；无呈现纹理或绑定失败则丢帧告警
2. `AV_PIX_FMT_NV12` → SDL_UpdateNVTexture
3. `AV_PIX_FMT_YUV420P` → SDL_UpdateYUVTexture
4. 其他格式 → sws_scale 转 YUV420P 后上传

#### Scenario: YUV420P 帧渲染
- **WHEN** Render 收到 format=AV_PIX_FMT_YUV420P 的 MediaFrame
- **THEN** RenderYUV420P 被调用，通过 RawFrame()->data/linesize 读取数据

#### Scenario: 硬件帧零拷贝渲染
- **WHEN** Render 收到携带呈现纹理的 D3D11 硬件帧且后端支持绑定
- **THEN** 经 `SDL_CreateTextureWithProperties(SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER)` 包装呈现纹理呈现，帧数据不发生 GPU→CPU 拷贝

#### Scenario: 呈现纹理缺失时丢帧而非跨线程转换
- **WHEN** D3D11 硬件帧未携带呈现纹理（解码侧未生成）或绑定失败
- **THEN** 该帧被丢弃并记录告警；渲染器 SHALL NOT 调用 av_hwframe_transfer_data（设备命令上下文禁止在渲染线程使用）

### Requirement: 渲染器后端能力声明
VideoRenderer SHALL 在 Open() 时探测 SDL 渲染器后端并暴露两项只读能力：`BindableHardwareDomain()`（可零拷贝绑定的硬件帧域，无则 kUnknown）与 `NativeDevice()`（后端原生设备指针，无则 nullptr）。二者 SHALL 仅由 Open/Close 修改，渲染线程与协商线程只读。

#### Scenario: D3D11 后端探测
- **WHEN** SDL 渲染器后端为 D3D11
- **THEN** `BindableHardwareDomain()` 返回 kD3D11，`NativeDevice()` 返回其 ID3D11Device 指针

#### Scenario: 非 D3D11 后端
- **WHEN** SDL 渲染器后端非 D3D11
- **THEN** 两查询分别返回 kUnknown 与 nullptr，VideoSinkNode 据此拒绝硬件域
