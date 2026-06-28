## REMOVED Requirements

### Requirement: IDecoder defines abstract decoder interface
**Reason**: IDecoder 接口（Open/SetFrameCallback/SetEofCallback/Start/Stop/SetDropUntilPts）的职责在新架构中被拆分：生命周期管理由 INode 统一处理（Configure/Prepare/Start/Stop），数据输出由 Port.Push() 替代回调，seek 跳帧由 DecoderNode 的 Process 内部处理。

**Migration**: 实现 DecoderNode（继承 INode），通过 OutputPort 推送帧，不再使用 MediaFrameCallback 和 EofOutputCallback。

### Requirement: IDecoder provides SetFrameCallback setter
**Reason**: 回调模式被 OutputPort.Push() 的推模式替代。

**Migration**: DecoderNode 解码出帧后直接 `output_port_->Push(MediaBuffer{frame})`。

### Requirement: IDecoder provides SetEofCallback setter
**Reason**: EOF 信号通过 MediaBuffer 的 kEos flag 沿链路传播，不再需要独立回调通道。

**Migration**: DecoderNode 在收到上游 kEos 后，冲刷内部解码器缓冲，最后输出一个 kEos MediaBuffer。

### Requirement: IDecoder acquires PacketQueue via Start
**Reason**: 数据来源从固定 PacketQueue 参数变为通用的 InputPort + Link 机制。

**Migration**: DecoderNode 从 InputPort.Pull() 拉取数据，不关心底层 Link 的类型和容量策略。
