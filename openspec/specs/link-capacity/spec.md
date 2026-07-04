## ADDED Requirements

### Requirement: Link 支持双维度容量约束

Link SHALL 支持同时按字节数（`max_bytes`）和条目数（`max_count`）限制队列容量，Push 时任一维度超限则阻塞。

#### Scenario: Packet 链路达到字节上限时阻塞

- **WHEN** 向 Link 中 Push 包，累计字节数超过 `max_bytes`
- **THEN** Push 阻塞，直到 Pop 释放空间后字节数降至上限以下

#### Scenario: Packet 链路达到条目上限时阻塞

- **WHEN** 向 Link 中 Push 包，`queue_.size()` 达到 `max_count`
- **THEN** Push 阻塞，直到 Pop 释放一个条目

#### Scenario: 帧链路仅按条目数限制

- **WHEN** `max_bytes` 设为不限（`INT64_MAX`）
- **THEN** Push 只按 `max_count` 阻塞，字节数维度不影响

### Requirement: Link 去模板化

Link SHALL 不再使用 `template<typename CapacityPolicy>`，移除 `PacketLink` / `FrameLink` 类型别名。

#### Scenario: 统一 Link 类型

- **WHEN** 创建 Link 实例
- **THEN** 类型为 `Link` 而非 `Link<ByteCapacity>` 或 `Link<CountCapacity>`

### Requirement: 各连接使用独立容量参数

- Demux→Decoder SHALL 使用 `{15MB, 256}`
- VideoDecoder→VideoSink SHALL 使用 `{不限, 3}`
- AudioDecoder→AudioSink SHALL 使用 `{不限, 9}`

#### Scenario: Demux→Decoder 不因视频包阻塞音频路径

- **WHEN** Demux 读取交织的 A/V 包
- **THEN** Packet 链路有足够容量（15MB / 256 包），Demux 不会被视频包反压阻塞
