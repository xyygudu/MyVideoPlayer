## 1. PacketQueue 改按字节数限制

- [x] 1.1 packet_queue.h：构造参数改为 `int64_t max_bytes = 15 * 1024 * 1024`，新增 `int64_t total_bytes_` 成员和 `int64_t ByteSize() const` 方法，`max_size_` 改为 `max_bytes_`
- [x] 1.2 packet_queue.cc：Push 时累加 `pkt->size` 到 `total_bytes_`，满队列判定改为 `total_bytes_ >= max_bytes_`；Pop 时扣减；Flush 时清零；日志输出字节数
- [x] 1.3 验证构建通过

## 2. FrameQueue 帧数调整

- [x] 2.1 frame_queue.h：默认 max_size 从 16 改为 4（作为通用默认值）
- [x] 2.2 player.cc：video_frame_queue_ 构造时显式传入 3
- [x] 2.3 audio_output.cc：audio_frame_queue_ 构造时从 64 改为 9
- [x] 2.4 验证构建通过

## 3. 验证

- [x] 3.1 完整构建验证（cmake --preset default + cmake --build build）
- [x] 3.2 运行 mvp_app.exe 打开视频文件，确认播放正常、日志中可见 PacketQueue 字节数信息
