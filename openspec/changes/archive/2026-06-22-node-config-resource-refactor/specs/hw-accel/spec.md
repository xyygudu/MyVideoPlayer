## MODIFIED Requirements

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
