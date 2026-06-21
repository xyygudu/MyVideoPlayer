## REMOVED Requirements

### Requirement: StreamContext aggregates pipeline components
**Reason**: StreamContext 硬编码了 PacketQueue + IDecoder + FrameQueue 的聚合关系，在新的图架构中这些组件被 Port + Link + INode 的动态组合替代。单个 StreamContext 无法描述多节点、多分支的拓扑，也无法支持 Transform 节点的插入。

**Migration**: 使用 MediaGraph::AddNode() + Connect() 构建图。原来 `StreamContext(decoder, frame_queue_size, max_packet_bytes)` 的创建逻辑分解为独立的 DecoderNode + Link 实例。

### Requirement: StreamContext provides unified Flush
**Reason**: Flush 操作由 MediaGraph::Flush() 统一广播到所有节点和 Link，不再需要中间层聚合。

**Migration**: 使用 `MediaGraph::Flush()` 替代。

### Requirement: StreamContext provides FlushAndDropUntil
**Reason**: Seek 快速跳帧功能由 DecoderNode 的 `SetDropUntilPts()` 保留，但调用方式从 StreamContext 改为直接调用 DecoderNode 方法。

**Migration**: `decoder_node->SetDropUntilPts(target_pts)` 后调用 `MediaGraph::Flush()`。
