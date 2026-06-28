## ADDED Requirements

### Requirement: HWAccelContext creates D3D11VA device

系统 SHALL 提供 `HWAccelContext` 类，负责探测系统是否支持 D3D11VA 硬件加速，并创建 `AVHWDeviceContext`。

`HWAccelContext::Create(AVHWDeviceType type)` SHALL 返回 `std::unique_ptr<HWAccelContext>`。创建失败时 SHALL 返回 nullptr（不抛异常）。

HWAccelContext SHALL 持有 `AVBufferRef*`（hw_device_ctx）的所有权，析构时释放。

#### Scenario: Successful creation on supported system
- **WHEN** 系统支持 D3D11VA 且调用 `HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA)`
- **THEN** 返回有效的 HWAccelContext 实例，`DeviceRef()` 返回非空 AVBufferRef*

#### Scenario: Creation fails on unsupported system
- **WHEN** 系统不支持 D3D11VA（如无 GPU 驱动）
- **THEN** 返回 nullptr，日志输出警告信息

#### Scenario: Device ref can be shared with codec context
- **WHEN** 调用 `DeviceRef()` 获取 AVBufferRef*
- **THEN** 可用 `av_buffer_ref()` 创建引用副本赋给 `codec_ctx->hw_device_ctx`

### Requirement: HWAccelContext provides get_format callback

HWAccelContext SHALL 提供静态方法 `GetFormat`，符合 FFmpeg `AVCodecContext::get_format` 回调签名。回调 SHALL 从 `codec_ctx->opaque` 获取 HWAccelContext 指针，在候选格式列表中查找硬件 pixel format，找到则返回该格式，否则返回软件格式作为 fallback。

#### Scenario: Hardware format available in candidates
- **WHEN** FFmpeg 在 `get_format` 候选列表中包含 `AV_PIX_FMT_D3D11`
- **THEN** 回调返回 `AV_PIX_FMT_D3D11`

#### Scenario: Hardware format not in candidates
- **WHEN** FFmpeg 在候选列表中不包含硬件格式
- **THEN** 回调返回列表中的软件格式（如 `AV_PIX_FMT_YUV420P`），解码自动回退软件模式

### Requirement: HWAccelContext exposes hardware pixel format

HWAccelContext SHALL 提供 `HWPixelFormat()` 方法返回该加速类型对应的 pixel format（如 D3D11VA 返回 `AV_PIX_FMT_D3D11`）。

#### Scenario: Query pixel format
- **WHEN** 调用 `HWPixelFormat()` on D3D11VA context
- **THEN** 返回 `AV_PIX_FMT_D3D11`

### Requirement: HWAccelContext owned at graph level
HWAccelContext 的创建和持有 SHALL 从 MediaPlayer::Impl 成员提升到 MediaGraph 的共享资源（`shared_ptr<HWAccelContext>`）。

MediaPlayer::BuildGraph() SHALL 在创建 graph 后立即调用 `graph_->SetHWDevice(HWAccelContext::Create(...))`。

DecoderNode 不再通过 SetHWAccel 外部注入 HW 指针，而是在 Prepare 中从 graph 查询。

#### Scenario: MediaPlayer injects HW device into graph
- **WHEN** MediaPlayer::BuildGraph() 构建 graph
- **THEN** 调用 `graph_->SetHWDevice(HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA))`

#### Scenario: HW device ownership transfer
- **WHEN** MediaPlayer::Close() 调用 graph_.reset()
- **THEN** graph 析构时如果 shared_ptr 引用计数归零则 HWAccelContext 被释放
