## MODIFIED Requirements

### Requirement: MediaGraph provides shared HW device resource
MediaGraph SHALL 提供 `SetGpuDevice(unique_ptr<gpu::GpuDevice>)` 和 `GpuDevice()` 接口，与 Clock 设计平行。GPU 设备作为管线级共享资源，可被解码、编码、特效与采集等多个节点共享。设备在管线构建期由编排器注入，注入只提供能力，不决定任何节点的格式选择。

#### Scenario: GPU device injected at graph level
- **WHEN** MediaPlayer 打开渲染器后将其设备包装为 GpuDevice 并 `graph->SetGpuDevice(...)`
- **THEN** `graph->GpuDevice()` 返回非空指针，直至 graph 析构

#### Scenario: No device keeps the pipeline software
- **WHEN** 渲染器后端无可用设备（包装返回 nullptr）
- **THEN** `graph->GpuDevice()` 返回 nullptr，各节点协商软格式，构图不受影响

#### Scenario: Multiple nodes share same GPU device
- **WHEN** 未来转码场景中 DecoderNode 和 EncoderNode 都需要 GPU 加速
- **THEN** 两者从同一 `graph->GpuDevice()` 获取同一设备引用
