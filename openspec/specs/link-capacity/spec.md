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

### Requirement: Link 只负责有界排队
`Link` SHALL 只承担两项职责：按双维度容量约束的阻塞排队，以及 flush / abort 时唤醒等待方。`Link` SHALL NOT 持有 seek 世代号 —— 世代是图级概念，由 `MediaGraph` 持有。

`Link::Flush()` SHALL 清空队列并唤醒两侧等待者，SHALL NOT 修改任何世代状态。

#### Scenario: Flush 不再递增世代
- **WHEN** 调用 Link::Flush()
- **THEN** 队列清空、两侧条件变量被唤醒，Link 内部不存在任何世代计数

#### Scenario: 陈旧数据的判定不依赖 Link
- **WHEN** 消费者判断一个 buffer 是否过期
- **THEN** 依据是图级世代，而非该 buffer 所经过的 Link 的状态

### Requirement: 各连接使用独立容量参数

- Demux→Decoder SHALL 使用 `{15MB, 256}`
- VideoDecoder→VideoSink SHALL 使用 `{不限, 3}`
- AudioDecoder→AudioSink SHALL 使用 `{不限, 9}`

#### Scenario: Demux→Decoder 不因视频包阻塞音频路径

- **WHEN** Demux 读取交织的 A/V 包
- **THEN** Packet 链路有足够容量（15MB / 256 包），Demux 不会被视频包反压阻塞
