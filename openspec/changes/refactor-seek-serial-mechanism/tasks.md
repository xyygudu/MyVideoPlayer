## 1. PacketQueue 添加 serial 机制

- [x] 1.1 添加 `SerialPacket` 内部结构体，`atomic<int> serial_`，修改 queue_ 类型为 `queue<SerialPacket>`
- [x] 1.2 添加 `FlushAndIncrementSerial()` 和 `serial()` 公共方法（Flush + serial++ 在同一把锁内执行）
- [x] 1.3 修改 `Push()` 使入队 packet 携带当前 serial
- [x] 1.4 修改 `Pop()` 签名增加 `int* serial` out 参数，返回 packet 的 serial

## 2. FrameQueue 添加 serial 机制

- [x] 2.1 添加 `SerialFrame` 内部结构体，`atomic<int> serial_`，修改 queue_ 类型
- [x] 2.2 添加 `FlushAndIncrementSerial()` 和 `serial()` 公共方法
- [x] 2.3 修改 `Push()` 使入队 frame 携带当前 serial
- [x] 2.4 修改 `Pop()` 签名增加 `int* serial` out 参数

## 3. Decoder 改用 serial 触发 flush

- [x] 3.1 添加 `int last_serial_{0}` 成员，移除 `flush_requested_` 和 `flush_completed_`
- [x] 3.2 移除 `RequestFlush()` 和 `FlushCompleted()` 方法
- [x] 3.3 修改 `DecodeLoop`：Pop 时获取 serial，serial 跳变则 flush codec 并更新 last_serial_
- [x] 3.4 Push frame 时传递当前 packet 的 serial 给 FrameQueue

## 4. Player::Seek() 简化

- [x] 4.1 移除 Seek() 中的 RequestFlush() 调用
- [x] 4.2 将现有 Flush() 调用改为 `FlushAndIncrementSerial()`
- [x] 4.3 移除 flush_completed_ 等待逻辑（render 线程中）

## 5. AudioOutput 适配

- [x] 5.1 添加 `FlushFrameQueue()` 方法暴露 audio_frame_queue_ 的 FlushAndIncrementSerial
- [x] 5.2 AudioLoop 中 Pop 获取 serial，丢弃 serial 不匹配的 frame
- [x] 5.3 AudioOutput 内部 decoder 适配新 Pop 签名

## 6. Demuxer 简化

- [x] 6.1 DemuxLoop 中移除 seek 后的 Flush 调用（旧 packet 由 serial 自然淘汰）

## 7. VideoRenderLoop 适配

- [x] 7.1 Pop frame 时获取 serial，丢弃 serial < 当前 serial 的旧帧
- [x] 7.2 移除 flush_completed_ 等待逻辑，保留 step_one_frame_ 标志

## 8. 验证

- [x] 8.1 编译通过，无 warning
- [ ] 8.2 播放状态 seek 正常跳转
- [ ] 8.3 暂停状态 seek 画面和帧号正确更新
