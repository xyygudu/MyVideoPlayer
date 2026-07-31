## ADDED Requirements

### Requirement: InputPort carries downstream needs_global_header requirement
InputPort SHALL 承载下游节点在协商期写入的 `needs_global_header` 需求，供上游节点在 Prepare 期读取。上游 Encoder 通过其输出端口连接的 peer（即该 InputPort）读取该值。

#### Scenario: MuxNode publishes requirement to upstream encoder
- **WHEN** `MuxNode::Negotiate()` 解析容器后对输入端口调用 `SetNeedsGlobalHeader(needs_global_header_)`
- **THEN** 上游 `EncoderNode` 在 `Prepare()` 通过 `output_port_->Peer()->NeedsGlobalHeader()` 读到该值

#### Scenario: Unset requirement defaults to false
- **WHEN** InputPort 尚未被写入该需求
- **THEN** `NeedsGlobalHeader()` 返回 false（编码器按带内头处理）
