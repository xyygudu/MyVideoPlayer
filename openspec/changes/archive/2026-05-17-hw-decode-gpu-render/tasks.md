## 1. HWAccelContext 实现

- [ ] 1.1 创建 `hw_accel_context.h`：类声明（Create, DeviceRef, HWPixelFormat, GetFormat 静态回调）
- [ ] 1.2 创建 `hw_accel_context.cc`：实现 `Create`（av_hwdevice_ctx_create）、`GetFormat` 回调逻辑
- [ ] 1.3 `CMakeLists.txt` 添加新文件到 mvp_core 目标，链接 `d3d11` `dxgi`

## 2. Decoder 集成硬件加速

- [ ] 2.1 `decoder.h` 修改 `Open` 签名：新增可选参数 `HWAccelContext* hw_ctx = nullptr`
- [ ] 2.2 `decoder.cc` `Open` 中：hw_ctx 非空时设置 `hw_device_ctx`、`opaque`、`get_format`
- [ ] 2.3 验证：hw_ctx 为 nullptr 时行为完全不变（回归）

## 3. VideoRenderer 硬件帧渲染路径

- [ ] 3.1 `video_renderer.h/cc` 新增 `RenderHWFrame` 私有方法（D3D11 texture → SDL_Texture 零拷贝）
- [ ] 3.2 `video_renderer.h/cc` 新增 `RenderNV12Frame` 私有方法（SDL_UpdateNV12Texture）
- [ ] 3.3 修改 `Render` 入口：根据帧 format 分支调用 RenderHWFrame / RenderNV12Frame / 原路径
- [ ] 3.4 调整 `Render` 接口使其能接收 AVFrame*（或从 VideoFrame 中获取 format 信息）

## 4. Player 层接入

- [ ] 4.1 `PlayerImpl` 新增 `std::unique_ptr<HWAccelContext> hw_accel_ctx_` 成员
- [ ] 4.2 `PlayerImpl::Open` 中尝试 `HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA)`
- [ ] 4.3 将 hw_accel_ctx_ 传入 `video_ctx_->OpenDecoder(stream, hw_accel_ctx_.get())`
- [ ] 4.4 `StreamContext::OpenDecoder` 转发 hw_ctx 参数给 `Decoder::Open`

## 5. 验证

- [ ] 5.1 编译通过，无新增 warning
- [ ] 5.2 运行程序，日志确认 `"HW decode: D3D11VA enabled"` 或 fallback 日志
- [ ] 5.3 播放 2K H.264 视频，验证画面正确、无花屏
- [ ] 5.4 Seek 测试：验证硬解下 seek 正常且延迟进一步降低
- [ ] 5.5 测试回退：模拟无 GPU 场景（或不支持的 codec），确认静默回退软解
