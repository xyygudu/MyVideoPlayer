## MODIFIED Requirements

### Requirement: seek 世代由 MediaGraph 持有

seek 世代 SHALL 是图级概念，由 `MediaGraph` 唯一持有。`Link` SHALL NOT 持有世代号。

递增 SHALL 由 `MediaGraph::Flush()` 承担，并 SHALL 发生在清空 Link **之前** —— flush 会唤醒阻塞在 Push 上的生产者，若世代在其后才递增，存在窗口期使被唤醒者推入的旧数据看起来属于当代。把递增与清空绑定在同一个操作内，可避免该不变量依赖调用方记得配对。

生产者节点 SHALL 在完成重定位时（`av_seek_frame` 之后）从图**锁存**世代，SHALL NOT 在每次产出数据时现读 —— 否则 "seek 前读出、seek 后推入" 的数据会被盖上新世代，绕过校验。

#### Scenario: 世代不依赖各链路一起递增

- **WHEN** 图中存在多条 Link（视频包、音频包、视频帧、音频帧链路）
- **THEN** 所有消费者读到的当前世代来自同一个图级计数器，不依赖各 Link 各自维护并保持同步

#### Scenario: 生产者锁存世代而非现读

- **WHEN** DemuxNode 在 seek 前已从容器读出一个包，seek 发生后才将其推入队列
- **THEN** 该包携带的是 seek **前**的世代，因而被下游丢弃

#### Scenario: 清空队列必然伴随世代递增

- **WHEN** 任何路径导致 Link 被清空
- **THEN** 世代必已递增，不存在"队列清空但在途数据仍被当作当代"的状态
