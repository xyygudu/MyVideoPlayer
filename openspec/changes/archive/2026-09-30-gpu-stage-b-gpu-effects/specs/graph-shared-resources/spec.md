## MODIFIED Requirements

### Requirement: MediaGraph provides shared HW device resource
MediaGraph SHALL 提供 `SetGpuDevice(std::shared_ptr<gpu::GpuDevice>)` 和 `GpuDevice()` 接口，与 Clock 设计平行。GPU 设备作为管线级共享资源，可被解码、编码、特效与采集等多个节点共享。graph 与提供设备的一方（渲染器）共享所有权；`GpuDevice()` 返回非拥有指针，在 graph 生命期内有效。设备在管线构建期由编排器注入，注入只提供能力，不决定任何节点的格式选择。

#### Scenario: GPU device injected at graph level
- **WHEN** MediaPlayer 打开渲染器后把渲染器持有的设备 `graph->SetGpuDevice(renderer.SharedGpuDevice())`
- **THEN** `graph->GpuDevice()` 返回非空指针，直至 graph 析构

#### Scenario: No device keeps the pipeline software
- **WHEN** 渲染器后端无可用设备（包装返回 nullptr）
- **THEN** `graph->GpuDevice()` 返回 nullptr，各节点协商软格式，构图不受影响

#### Scenario: Multiple nodes share same GPU device
- **WHEN** DecoderNode 与特效节点都需要 GPU 设备
- **THEN** 两者从同一 `graph->GpuDevice()` 获取同一设备引用
