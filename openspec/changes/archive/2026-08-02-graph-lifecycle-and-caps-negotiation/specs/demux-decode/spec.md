## REMOVED Requirements

### Requirement: DemuxNode 实现 ISourceNode::Probe
**Reason**: `ISourceNode` 接口与 `graph::StreamInfo` 结构均已不存在于代码中——源探测由独立的 `SourceProbe`/`SourceInfo` 承担，图内格式由端口 `MediaFormat` 承载。本次删除 `graph::StreamInfo` 死代码后，该需求已彻底失效。
**Migration**: 探测使用 `SourceProbe::Probe()` 返回 `SourceInfo`；DemuxNode 在 `Open()` 打开文件、`Negotiate()` 从 codecpar 构造 `EncodedFormat` 发布到输出端口（见 `graph-source-nodes`）。
