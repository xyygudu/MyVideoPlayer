## Why

Stage A 让硬解帧零拷贝呈现，但特效仍是纯 CPU：任何特效一启用，4K 硬件帧就要在解码线程 `av_hwframe_transfer_data` 下载到内存（约 12MB/帧），处理完再以软件帧上传，GPU 解码的收益全部丢失。同时 Stage A 的 GPU 资源设计有四处结构性问题，是 GPU 特效的前置障碍：

1. **锁**：应用层 `std::mutex` 包住整个 `avcodec_send_packet`（4K 关键帧持锁 10ms+ 阻塞渲染），而 FFmpeg 内部另有一把设备锁，两把锁互不知情；非递归锁也无法与 FFmpeg 的锁合并。
2. **所有权倒置**：设备锁与呈现纹理池属于 graph 生命周期的 `GpuDevice`，却被生命周期更长的渲染器以裸指针使用，靠 `Close()` 里手动置空维持。
3. **呈现池无生命周期跟踪**：8 张环形纹理的复用依赖"池大小 > 在途帧数"的隐式不变量；分辨率变化时整池释放，在途帧指针悬空。
4. **职责错位**：`CopyForPresentation`（因 SDL 不能包装数组纹理才需要的呈现关注点）放在设备里、由解码器调用。

## What Changes

- **设备所有权**：渲染器（输出侧）包装并以 `shared_ptr` 持有 `GpuDevice`，graph 共享持有；删除 `VideoRenderer::SetDeviceContextMutex` 与裸指针契约 **BREAKING（内部接口）**
- **统一递归设备锁**：`GpuDevice` 在 `av_hwdevice_ctx_init` 之前把一把递归锁交给 FFmpeg（`AVD3D11VADeviceContext.lock/unlock`），锁的生命期绑定 FFmpeg 设备上下文；FFmpeg 解码、GPU 特效、呈现拷贝、SDL 绘制共用这一把锁，粒度为单次 GPU 操作。`DecoderNode` 不再加锁
- **解码帧池细化**：`DecoderNode` 在 `get_format` 中自建帧池，经 `GpuDevice::RefineDecoderFrames` 加 `D3D11_BIND_SHADER_RESOURCE`（mpv `hwframes_refine`），并用 `extra_hw_frames` 为下游在途帧预留 surface
- **GPU 视频 pass**：gpu 层新增 `VideoPass` 接口与 D3D11 实现：以 HLSL 像素着色器读取硬件帧（数组切片零拷贝），写入 FFmpeg 帧池分配的独立纹理（引用计数随帧释放）。着色器在构建期由 `fxc` 编译进二进制
- **共享立即上下文的状态隔离**：GPU pass 保存/恢复其触碰的管线状态（SDL3 的输入布局、顶点着色器只在建设备时设置一次，不会重设）
- **特效节点双实现**：`ColorEffectNode`/`TransformEffectNode` 对硬件帧走 GPU pass，软件帧走原 CPU 路径，GPU 不可用时回退下载；开关特效不再改变帧域
- **呈现适配器**：删除设备呈现环形池、`CopyForPresentation`、`MediaFrame::HwPresentationTexture`；渲染器持有 `FramePresenter`，可直接绑定的纹理（独立 + SRV + 尺寸匹配）零拷贝，数组纹理在渲染线程拷贝进呈现器自有纹理（mpv d3d11 1-copy 路径）**BREAKING（内部接口）**

## Capabilities

### New Capabilities
<!-- 无：GPU pass 与呈现适配器属于 gpu-device 能力的扩展 -->

### Modified Capabilities
- `gpu-device`：接口增加帧池细化、上下文锁、视频 pass 与呈现适配器工厂；设备改为共享所有权；线程安全要求改为"单一递归锁交给 FFmpeg"；删除呈现纹理生成服务；新增共享上下文状态隔离要求
- `graph-shared-resources`：`SetGpuDevice` 改为共享所有权
- `demux-decode`：删除"硬件帧在解码线程完成呈现准备"；新增解码帧池细化与"解码器不持有设备锁"
- `media-frame`：删除"硬件帧携带呈现纹理"
- `video-renderer`：渲染器包装并持有设备；硬件帧经呈现适配器绑定，不可绑定布局在锁内下载
- `graph-effect-nodes`：两个特效节点对硬件帧走 GPU pass，软件帧走 CPU，GPU 不可用回退下载

## Impact

- 代码：`src/media/gpu/`（接口重构 + `d3d11_video_pass`/`d3d11_frame_presenter`/`d3d11_util` 新增 + `shaders/*.hlsl`）、`src/media/CMakeLists.txt`（fxc 构建步骤）、`media_frame.{h,cc}`、`graph/media_graph.{h,cc}`、`nodes/decoder_node.{h,cc}`、`nodes/color_effect_node.{h,cc}`、`nodes/transform_effect_node.{h,cc}`、`video_renderer.{h,cc}`、`media_player.cc`
- 构建依赖：需要 Windows SDK 的 `fxc.exe`（找不到时配置期报错）
- 不影响：音频链路、软件解码路径、转码链路（本期仍不注入设备）
- 不在本期：图自动插入域转换节点（双实现 + 按帧域分派使链路保持域不变，暂不需要）、渲染器跨文件存活、视图缓存、跨 seek 复用帧池
