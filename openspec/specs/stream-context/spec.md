## Requirements

### Requirement: StreamContext provides unified Abort
StreamContext SHALL 提供 Abort() 方法，依次调用 packet_queue_.Abort()、frame_queue_.Abort() 和 decoder_->Stop()。

### Requirement: StreamContext provides Reset
StreamContext SHALL 提供 Reset() 方法，将队列恢复到初始状态。

### Requirement: StreamContext provides Start and Stop
Start() SHALL 调用 decoder_->Start()，Stop() SHALL 调用 decoder_->Stop()。

### Requirement: StreamContext provides PopFrame
PopFrame() 从内部 frame_queue 弹出帧。

### Requirement: StreamContext provides CurrentSerial
CurrentSerial() 返回 packet_queue 的当前 serial 值。

### Requirement: StreamContext provides queue accessors for wiring
GetPacketQueue() 和 GetFrameQueue() 用于组件间连线。

### Requirement: StreamContext provides OpenDecoder
OpenDecoder 初始化解码器并注入回调。
