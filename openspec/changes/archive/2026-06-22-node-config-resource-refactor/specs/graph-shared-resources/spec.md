## ADDED Requirements

### Requirement: MediaGraph provides shared HW device resource
MediaGraph SHALL 提供 `SetHWDevice(shared_ptr<HWAccelContext>)` 和 `HWDevice()` 接口，与 Clock 设计平行。HW 设备作为管线级共享资源，可被多个节点（解码器、未来编码器）共享。

#### Scenario: HW device injected at graph level
- **WHEN** MediaPlayer 构建 graph 后调用 `graph->SetHWDevice(hw_accel)`
- **THEN** graph->HWDevice() 返回有效的 HWAccelContext 共享指针

#### Scenario: DecoderNode queries HW from graph
- **WHEN** DecoderNode::Prepare() 执行且 media_type 为 kVideo
- **THEN** 通过 graph_->HWDevice() 查询 HW 设备，若存在则配置硬件加速

#### Scenario: Multiple nodes share same HW device
- **WHEN** 未来转码场景中 DecoderNode 和 EncoderNode 都需要 HW 加速
- **THEN** 两者从同一 graph->HWDevice() 获取同一设备引用，支持 GPU 内零拷贝

### Requirement: Node accesses graph via SetGraph pattern
需要 graph 级资源的节点 SHALL 通过 `SetGraph(MediaGraph*)` 注入 graph 指针（非拥有），在 Prepare/Start 中查询共享资源。此模式与 VideoSinkNode/AudioSinkNode 已有的 SetGraph 一致。

#### Scenario: DecoderNode receives graph reference
- **WHEN** MediaPlayer 创建 DecoderNode 后调用 SetGraph(graph)
- **THEN** DecoderNode 持有 graph 非拥有指针，Prepare 中可查询 HWDevice
