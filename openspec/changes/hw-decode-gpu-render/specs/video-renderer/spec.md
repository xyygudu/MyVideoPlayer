## ADDED Requirements

### Requirement: VideoRenderer supports D3D11 hardware frame zero-copy

VideoRenderer SHALL 检测传入帧的格式。当帧为硬件格式（`AV_PIX_FMT_D3D11`）时，SHALL 使用 `SDL_CreateTextureWithProperties` 将帧中的 `ID3D11Texture2D*` 直接绑定为 SDL_Texture，实现零拷贝渲染。

渲染完成后 SHALL 销毁临时 SDL_Texture（不销毁底层 D3D11 texture，其生命周期由 AVFrame 管理）。

#### Scenario: Render a D3D11VA hardware frame
- **WHEN** VideoRenderer 接收到 format == AV_PIX_FMT_D3D11 的帧
- **THEN** 从 frame->data[0] 获取 ID3D11Texture2D*，通过 SDL properties 绑定为纹理并渲染，无 GPU→CPU 拷贝

#### Scenario: Hardware frame maintains aspect ratio
- **WHEN** 硬件帧尺寸与窗口比例不同
- **THEN** 保持宽高比居中渲染，与软件帧行为一致

#### Scenario: Texture cleanup after render
- **WHEN** 一帧硬件帧渲染完成
- **THEN** SDL_Texture wrapper 被销毁，底层 D3D11 texture 不受影响（由 AVFrame unref 管理）

### Requirement: VideoRenderer supports NV12 software upload

当帧格式为 NV12（常见于硬解 transfer 后或某些 codec 原始输出）时，VideoRenderer SHALL 使用 `SDL_UpdateNV12Texture` 直接上传，无需 sws_scale 转换为 YUV420P。

#### Scenario: Render NV12 frame without conversion
- **WHEN** VideoRenderer 接收到 format == AV_PIX_FMT_NV12 的 CPU 帧
- **THEN** 使用 SDL_UpdateNV12Texture 上传并渲染，不调用 sws_scale

## MODIFIED Requirements

### Requirement: VideoRenderer renders YUV frames via SDL3 GPU
系统 SHALL 在 core 层提供 `VideoRenderer` 类，使用 SDL3 的 GPU 加速后端渲染视频帧。

**渲染路径选择逻辑**（按帧格式分支）：
1. `AV_PIX_FMT_D3D11` → 零拷贝 D3D11 texture 直通（新增）
2. `AV_PIX_FMT_NV12`（CPU）→ SDL_UpdateNV12Texture（新增）
3. `YUV420P` → SDL_UpdateYUVTexture（原路径）
4. 其他格式 → sws_scale 转 YUV420P 后上传（原 fallback）

VideoRenderer SHALL 根据输入帧的 `format` 字段自动选择渲染路径，无需外部指定。

#### Scenario: Render a YUV420P frame
- **WHEN** VideoRenderer 接收到一帧 YUV420P 的 VideoFrame
- **THEN** 帧数据上传至 GPU 纹理并渲染到窗口，画面正确显示

#### Scenario: Render a D3D11 hardware frame
- **WHEN** VideoRenderer 接收到 format == AV_PIX_FMT_D3D11 的帧
- **THEN** 零拷贝绑定 D3D11 texture 并渲染

#### Scenario: Resolution change during playback
- **WHEN** 视频帧分辨率发生变化（如切换文件）
- **THEN** VideoRenderer 重新创建匹配新分辨率的纹理，继续正常渲染

#### Scenario: Maintain aspect ratio
- **WHEN** 渲染窗口尺寸与视频宽高比不一致
- **THEN** 视频画面保持原始宽高比居中显示，空余区域填黑
