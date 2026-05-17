## 1. IDecoder 接口变更

- [x] 1.1 `i_decoder.h`: 新增 `virtual void SetFrameCallback(MediaFrameCallback cb) = 0` 纯虚方法
- [x] 1.2 `i_decoder.h`: 新增 `virtual void SetEofCallback(EofOutputCallback cb) = 0` 纯虚方法
- [x] 1.3 `i_decoder.h`: `Start()` 签名从 `Start(PacketQueue*, MediaFrameCallback, EofOutputCallback)` 改为 `Start(PacketQueue*)`

## 2. AVFrameDecoder 适配

- [x] 2.1 `decoder.h`: 新增 `MediaFrameCallback on_frame_` 和 `EofOutputCallback on_eof_` 成员变量
- [x] 2.2 `decoder.h/cc`: 实现 `SetFrameCallback` 和 `SetEofCallback`，存储回调到成员
- [x] 2.3 `decoder.cc`: `Start()` 移除回调参数，改为使用存储的成员回调；入口处 assert 回调已设置

## 3. StreamContext 调用时序调整

- [x] 3.1 `stream_context.cc`: `OpenDecoder()` 在 `decoder_->Open()` 成功后调用 `SetFrameCallback` 和 `SetEofCallback`
- [x] 3.2 `stream_context.cc`: `Start()` 移除回调 lambda 创建，仅调用 `decoder_->Start(&packet_queue_)`

## 4. 验证

- [x] 4.1 编译通过，无新增 warning
- [x] 4.2 运行播放器，验证播放/暂停/Seek/EOF 均正常
