## MODIFIED Requirements

### Requirement: EOS 标记必须携带世代

`MediaBuffer::MakeEos(int serial)` SHALL 强制要求调用方传入世代号，SHALL NOT 提供使用默认值的重载。

理由：校验下沉到端口边界后，任何未携带正确世代的 EOS 都会被丢弃，导致播放永远不报结束且无任何报错。由编译器强制传参可杜绝此类遗漏。

`MakeEos` SHALL NOT 接收媒体类型 —— EOS 送往哪一路由输出端口的选择决定，类型字段不参与路由。

#### Scenario: 解码器 EOS 可抵达 sink
- **WHEN** 文件播放至结尾，DecoderNode drain 完成后发出 EOS
- **THEN** 该 EOS 携带当前世代，通过端口校验抵达 sink，播放状态转为结束

#### Scenario: 陈旧 EOS 被丢弃
- **WHEN** 播放至结尾后立即 seek，队列中残留 pre-seek 的 EOS
- **THEN** 该 EOS 因世代过期被丢弃，SHALL NOT 导致 seek 后立刻误报播放结束

#### Scenario: EOS 的去向由端口决定
- **WHEN** DemuxNode 在文件末尾同时向视频与音频输出端口发送 EOS
- **THEN** 两个 EOS 内容一致，各自到达对应下游，无需携带媒体类型加以区分
