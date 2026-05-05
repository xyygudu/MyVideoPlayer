## Why

当前全链路为 CPU 软解码 → SDL_UpdateYUVTexture 上传。2K/4K H.264 视频解码占 78% CPU，seek 延迟 300-700ms。上一轮 skip_frame 优化将中位数降至 271ms，但软解本身的物理瓶颈（每帧 ~5ms）无法突破。启用 D3D11VA 硬解后每帧 <0.1ms，且解码输出直接为 GPU texture，可以零拷贝送入 SDL3 渲染——解码+渲染全程 GPU，彻底消除 CPU 解码瓶颈和跨总线拷贝。

## What Changes

- 新增 `HWAccelContext` 类：探测并创建 D3D11VA 硬件加速设备，管理 `AVHWDeviceContext` 生命周期
- `Decoder::Open` 增加可选 `HWAccelContext*` 参数，注入硬件设备上下文 + 注册 `get_format` 回调
- `VideoRenderer` 增加硬件帧渲染路径：D3D11 texture 直接绑定为 SDL_Texture，零拷贝渲染
- `PlayerImpl::Open` 中尝试创建 HWAccelContext，失败则自动回退软解（对用户透明）
- 移除硬件帧走 sws_scale fallback 的路径（NV12 直接 SDL_UpdateNV12Texture 或 D3D11 直通）

## Capabilities

### New Capabilities

- `hw-accel`: 硬件加速设备探测、创建、生命周期管理，以及 Decoder 集成

### Modified Capabilities

- `demux-decode`: Decoder::Open 新增 HWAccelContext 注入和 get_format 回调
- `video-renderer`: VideoRenderer 新增硬件帧零拷贝渲染路径（D3D11 texture → SDL_Texture）

## Impact

- 新增文件：`src/core/src/hw_accel_context.h` / `hw_accel_context.cc`
- 修改文件：`src/core/src/decoder.h/cc`、`src/core/src/video_renderer.h/cc`、`src/core/src/player.cc`
- 新增依赖：`d3d11.h`、`dxgi.h`（Windows SDK，MSVC 已自带）
- CMakeLists.txt：链接 `d3d11.lib`、`dxgi.lib`
- 行为变更：默认尝试硬解，失败自动回退，对外 API 无变化
