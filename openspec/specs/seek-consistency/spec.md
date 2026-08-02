## Purpose

保证 seek 操作在 demux、decoder、Link 三者之间的一致性：seek 请求不丢失、demux 与 decoder 的目标一致、陈旧在途数据被正确隔离、seek 控制路径线程安全。

## Requirements

### Requirement: demux seek 请求不可丢失

DemuxNode 的 seek 请求机制 SHALL 保证：在任意快速连续 seek 序列下，demux 线程最终 seek 到的位置 SHALL 与最后一次 seek 命令的目标一致，不得因并发丢失最新请求。

#### Scenario: 处理旧请求时到来新请求不被吞掉

- **WHEN** demux 线程正在处理 seek 到 A，期间主线程请求 seek 到 B
- **THEN** demux 完成 A 后 SHALL 继续 seek 到 B（不会停在 A）

#### Scenario: 多次快速 seek 只生效最新目标

- **WHEN** 主线程连续请求 seek 到 X1, X2, X3
- **THEN** demux 最终 seek 到的位置 SHALL 是 X3

### Requirement: demux 与 decoder 的 seek 目标一致

在任意 seek 序列结束后，DemuxNode 实际 seek 到的位置与各 DecoderNode 的 drop 目标 SHALL 对应同一次 seek 命令，二者不得指向不同的 seek。

#### Scenario: 快速 seek 后不出现目标错配

- **WHEN** 用户连续快速 seek 多次后停止
- **THEN** demux 喂出的包区间与 decoder 的 drop_until_pts SHALL 匹配，解码器不会长时间丢弃所有帧

### Requirement: seek flush 隔离陈旧在途数据

seek flush 之后，seek 前产生但尚未被消费的数据 SHALL NOT 被当作 post-seek 有效数据交给下游。隔离 SHALL 覆盖全部三类在途数据：

1. 阻塞在 `Link::Push` 上、生产者手中尚未入队的 buffer
2. 已入队但尚未被 Pop 的 buffer
3. 已进入 Passive 节点 `Process()` 调用链、尚未抵达下游 Link 的 buffer

隔离 SHALL 通过世代号（seek epoch）实现，而非依赖清空队列 —— 清空只能处理第 2 类。

**帧与包同等对待**：DecoderNode 的输出 SHALL 携带其所解包的世代；VideoSinkNode / AudioSinkNode SHALL NOT 渲染或播放过期世代的帧。

#### Scenario: 被唤醒的陈旧包被丢弃

- **WHEN** 一个 pre-seek 包阻塞在 Link::Push，此时发生 seek flush
- **THEN** 该包 SHALL 因世代落后而被丢弃，不进入解码器

#### Scenario: 被唤醒的陈旧帧被丢弃

- **WHEN** 暂停状态下 seek，解码线程正阻塞在帧链路的 Push 上、手中持有一帧 pre-seek 数据
- **THEN** 该帧 SHALL 因世代落后而被丢弃，VideoSinkNode SHALL NOT 显示它

#### Scenario: 暂停态 seek 显示目标帧

- **WHEN** 暂停状态下 seek 到位置 T
- **THEN** 画面 SHALL 更新为 T 附近的帧，而非 seek 前的最后一帧

#### Scenario: flush 后首个解码包为 post-seek 数据

- **WHEN** seek 完成后解码器从 Link 取包
- **THEN** 解码器 SHALL NOT 在 avcodec_flush_buffers 之后立即解码到 pre-seek 的 GOP 中间帧

### Requirement: seek 世代由 MediaGraph 持有

seek 世代 SHALL 是图级概念，由 `MediaGraph` 唯一持有并在 `Seek()` 中递增。`Link` SHALL NOT 持有世代号。

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

### Requirement: 世代与有效性校验在端口边界统一执行

`InputPort::Pull()` SHALL 丢弃世代过期的 buffer 与无效载荷的 buffer，只向调用者返回可直接处理的有效数据。消费者节点 SHALL NOT 各自重复实现世代校验。

丢弃 SHALL 记录日志：世代过期属预期行为，记 DEBUG 级；载荷无效属非预期，记 WARN 级。二者 SHALL NOT 静默丢弃。

#### Scenario: 新增消费者节点天然受保护

- **WHEN** 向图中加入一个新的 Active 消费者节点，其代码未包含任何世代判断
- **THEN** 该节点仍不会收到过期数据

#### Scenario: 丢弃行为可观测

- **WHEN** 开启 DEBUG 日志并执行 seek
- **THEN** 日志中 SHALL 出现被丢弃的过期 buffer 记录，可据此确认隔离生效

### Requirement: EOS 标记必须携带世代

`MediaBuffer::MakeEos()` SHALL 强制要求调用方传入世代号，SHALL NOT 提供使用默认值的重载。

理由：校验下沉到端口边界后，任何未携带正确世代的 EOS 都会被丢弃，导致播放永远不报结束且无任何报错。由编译器强制传参可杜绝此类遗漏。

#### Scenario: 解码器 EOS 可抵达 sink

- **WHEN** 文件播放至结尾，DecoderNode drain 完成后发出 EOS
- **THEN** 该 EOS 携带当前世代，通过端口校验抵达 sink，播放状态转为结束

#### Scenario: 陈旧 EOS 被丢弃

- **WHEN** 播放至结尾后立即 seek，队列中残留 pre-seek 的 EOS
- **THEN** 该 EOS 因世代过期被丢弃，SHALL NOT 导致 seek 后立刻误报播放结束

### Requirement: seek 控制路径线程安全

seek 命令的响应 SHALL NOT 从非解码线程写 `AVCodecContext`。`SetDropUntilPts` SHALL 只更新原子 drop 目标，`skip_frame` 等 codec 字段 SHALL 仅由解码线程设置。

#### Scenario: 主线程不触碰 codec_ctx

- **WHEN** 主线程通过 OnCommand 调用 SetDropUntilPts
- **THEN** 该调用 SHALL NOT 写 codec_ctx_->skip_frame，仅原子写 drop_until_pts_
