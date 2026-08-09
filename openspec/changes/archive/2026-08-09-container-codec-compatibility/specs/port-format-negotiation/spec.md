## ADDED Requirements

### Requirement: FormatCaps carries codec compatibility constraint
`FormatCaps` SHALL 包含 `codec_ids` 维度（`std::vector<int>`，空表示无约束），用于声明某端口能产出或能接受的编码器集合。`FormatCaps::Compatible()` SHALL 在该维度双方都非空且交集为空时判定为不兼容，与其余维度使用相同的"双方都约束、且无交集才拒绝"规则；`FormatCaps::Intersect()` SHALL 对该维度取交集。

#### Scenario: 编码器把自身声明为单一约束
- **WHEN** EncoderNode::DeclareCaps() 解析出编码器
- **THEN** 其输出端口 caps 的 `codec_ids` 为该编码器 codec_id 组成的单元素集合

#### Scenario: 不兼容的编码器使协商失败
- **WHEN** 上游端口声明的 `codec_ids` 与下游端口声明的 `codec_ids` 无交集
- **THEN** `FormatCaps::Compatible()` 返回 false，`MediaGraph::Negotiate()` 在 `ValidateCaps()` 阶段失败，早于任何 `avcodec_open2`/`avformat_write_header` 调用

#### Scenario: 未声明 codec_ids 的端口接受任意编码器
- **WHEN** 某端口的 caps 未设置 `codec_ids`（保持默认空）
- **THEN** 该维度不参与兼容性判定，视为无约束
