## 1. gpu 层接口

- [x] 1.1 `gpu/video_pass.h`:`VideoKernel`、`ColorAdjustUniforms`/`AffineRemapUniforms`(16 字节对齐 static_assert)、`VideoPass` 接口
- [x] 1.2 `gpu/frame_presenter.h`:`FramePresenter` 接口
- [x] 1.3 `gpu/gpu_device.h`:删除 `CopyForPresentation`/`DeviceContextMutex`;新增 `RefineDecoderFrames`、`LockContext/UnlockContext`、`CreateVideoPass`、`CreatePresenter`、`ScopedContextLock`;`WrapExternal` 返回 `shared_ptr`

## 2. D3D11 后端

- [x] 2.1 `gpu/d3d11_util.h`:sw_format→DXGI/平面视图格式映射、`Align2`、FFmpeg 锁 RAII
- [x] 2.2 `D3D11GpuDevice::Wrap`:init 前交出递归锁(锁随 `AVHWDeviceContext.free` 释放)、检测 NV12 渲染目标支持;删除环形呈现池
- [x] 2.3 `RefineDecoderFrames`:支持采样时加 `D3D11_BIND_SHADER_RESOURCE`
- [x] 2.4 `D3D11FramePresenter`:可直接绑定判定 + 可见区域拷贝到自有纹理
- [x] 2.5 `D3D11VideoPass`:输出帧池(独立纹理 RT|SRV)、源 SRV(无 SRV 时暂存拷贝)、两级常量缓冲、每平面全屏三角形绘制
- [x] 2.6 管线状态保存/恢复(ImGui dx11 备份模式),所有 GPU 命令在设备锁内

## 3. HLSL 与构建

- [x] 3.1 `gpu/shaders/`:`video_pass_common.hlsli`、`fullscreen_vs.hlsl`、`color_adjust_ps.hlsl`、`affine_remap_ps.hlsl`(PSLuma/PSChroma)
- [x] 3.2 `src/media/CMakeLists.txt`:查找 `fxc`(找不到配置期报错)、构建期编译为字节码头文件并加入 mvp_core
- [x] 3.3 重新 `cmake --preset default`,着色器与 C++ 编译通过

## 4. 解码器

- [x] 4.1 删除 `DeviceLock`/`HwDevice()` 及所有应用层加锁
- [x] 4.2 删除 `ToDownstreamFrame` 呈现准备,直接推送原生硬件帧
- [x] 4.3 `get_format` 自建帧池(`avcodec_get_hw_frames_parameters` + 补齐工作 surface + `RefineDecoderFrames`,失败回退默认);带硬件打开设置 `extra_hw_frames`

## 5. 帧、图、渲染器与编排

- [x] 5.1 `MediaFrame`:删除呈现纹理字段与移动语义中的携带
- [x] 5.2 `MediaGraph::SetGpuDevice(shared_ptr)`
- [x] 5.3 `VideoRenderer`:Open 包装并持有设备 + 呈现器;删除 `SetDeviceContextMutex`/`NativeDevice()`;Render 持 `ScopedContextLock`;硬件帧经呈现器绑定,不可绑定布局锁内下载
- [x] 5.4 `MediaPlayer`:BuildGraph 注入 `SharedGpuDevice()`;Close 删除锁指针置空;BuildGraph 拆出 WireVideoBranch/WireAudioBranch(≤50 行)

## 6. 特效节点

- [x] 6.1 `ColorEffectNode`:Attach 取 graph;Prepare 创建 kColorAdjust pass;Process 硬件帧走 GPU、失败/不可用回退 CPU;函数 ≤50 行
- [x] 6.2 `TransformEffectNode`:同上,kAffineRemap,逆矩阵复用 `pixel_ops::ComputeAffineMapping`

## 7. 验证

- [x] 7.1 构建通过(mvp_core / mvp_app / mvp_transcode_cli),触及函数 ≤50 行;转码 CLI 冒烟(软解路径)通过
- [x] 7.2 硬解播放无特效:画面正常(解码数组帧经呈现器拷贝)
- [x] 7.3 硬解 + 色彩特效:饱和度 0 整幅灰度、亮度/对比度生效;日志显示 GPU pass,无下载
- [x] 7.4 硬解 + 几何特效:任意角度旋转/翻转/缩放/平移生效,越界黑边
- [x] 7.5 seek、暂停中 seek、重开文件、特效开关反复切换无卡死、无画面错乱
- [x] 7.6 软解视频 + 特效仍走 CPU 路径正常
- [x] 7.7 `openspec validate gpu-stage-b-gpu-effects --strict` 通过
