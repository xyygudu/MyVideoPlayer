## 1. Link 数据结构重构

- [x] 1.1 在 `link.h` 中新增 `LinkCapacity` 结构体（含 `max_bytes` 和 `max_count`），移除 `ByteCapacity` / `CountCapacity` / `PacketLink` / `FrameLink`
- [x] 1.2 将 `Link` 从模板类改为普通类，新增 `count_` 成员，`Push` 阻塞条件改为 `count_ >= max_count_ || total_bytes_ >= max_bytes_`

## 2. Port 接口适配

- [x] 2.1 在 `port.h` 中将 `FrameLink*` / `unique_ptr<FrameLink>` 替换为 `Link*` / `unique_ptr<Link>`
- [x] 2.2 在 `port.cc` 中将 `Connect` 参数从 `int link_capacity` 改为 `LinkCapacity`，构造 `Link` 而非 `FrameLink`

## 3. MediaGraph 接口适配

- [x] 3.1 在 `media_graph.h` 中将 `Connect` 签名从 `int link_capacity = 4` 改为 `LinkCapacity capacity = {}`

## 4. MediaPlayer 连接参数更新

- [x] 4.1 更新 `media_player.cc` 中 4 处 `Connect` 调用：
  - `Demux→VideoDecoder`: `{15 * 1024 * 1024, 256}`
  - `Demux→AudioDecoder`: `{15 * 1024 * 1024, 256}`
  - `VideoDecoder→VideoSink`: `{INT64_MAX, 3}`
  - `AudioDecoder→AudioSink`: `{INT64_MAX, 9}`

## 5. 编译验证

- [x] 5.1 编译验证：`cmake --build build` 确认无编译错误
