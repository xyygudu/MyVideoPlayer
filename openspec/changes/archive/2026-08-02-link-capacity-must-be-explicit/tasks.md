## 1. 容量必须显式且不可无界

- [x] 1.1 `LinkCapacity` 构造函数私有化，字段改为 `max_bytes_` / `max_count_` 加只读访问器
- [x] 1.2 新增静态工厂 `ForPackets()` 与 `ForFrames(int depth)`
- [x] 1.3 移除 `Link` 构造函数、`OutputPort::Connect`、`MediaGraph::Connect` 三处的容量默认值
- [x] 1.4 容量常量 `kPacketQueueBytes` / `kPacketQueueCount` / `kFrameQueueByteCap` 具名于 link.h 并注明 ffplay 出处

## 2. 字节维度对帧生效

- [x] 2.1 `ByteSize` 对帧遍历 `AVFrame::buf[]` 累加真实字节
- [x] 2.2 补充 `extended_buf[]`，覆盖超过 8 平面的 planar 音频
- [x] 2.3 EOS-only buffer 返回 0；更新注释说明硬件帧天然计为约 0

## 3. 调用点迁移

- [x] 3.1 `media_player.cc`：4 处 `Connect` 改用工厂；深度常量 `kVideoFrameDepth=3` / `kAudioFrameDepth=9` 具名并注明 ffplay 出处
- [x] 3.2 `transcoder.cc`：3 处 `Connect` 改用工厂；`kTranscodeFrameDepth=4` 具名并说明取值依据
- [x] 3.3 确认全项目无残留的字面量容量与无默认参数调用

## 4. 验证

- [x] 4.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 4.2 **峰值内存隔离测量**：4K 3733MB → 3407MB（−326MB）；480p 仅 191MB，4K/480p 内存比 17.8× ≈ 像素面积比 20.2×，证实剩余占用位于第三方库内部而非本队列
- [x] 4.3 转码回归：mkv 70,534,409B / mpegts 78,101,028B 与改动前逐字节一致，耗时 26.4s 无明显变化
- [x] 4.4 播放侧等价性：容量数值（3 帧 / 9 帧 / 15MB / 256 包）与改动前完全一致，仅构造方式改变；全量重编后转码回归复测仍逐字节一致
