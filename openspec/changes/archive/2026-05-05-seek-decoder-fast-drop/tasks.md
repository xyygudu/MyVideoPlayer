## 1. Decoder 接口扩展

- [x] 1.1 在 `decoder.h` 中添加 `std::atomic<double> drop_until_pts_{0}` 成员
- [x] 1.2 在 `decoder.h` 中声明 `void SetDropUntilPts(double pts)` 公有方法
- [x] 1.3 在 `decoder.cc` 中实现 `SetDropUntilPts`（atomic store）

## 2. DecodeLoop 修改

- [x] 2.1 serial change 分支中：flush 后检查 `drop_until_pts_` > 0 则设 `codec_ctx_->skip_frame = AVDISCARD_NONREF`
- [x] 2.2 receive_frame 循环中：计算 frame_pts，若 < `drop_until_pts_` 则跳过入队（continue）
- [x] 2.3 receive_frame 循环中：首个 frame_pts >= `drop_until_pts_` 时恢复 `skip_frame = AVDISCARD_DEFAULT` 并清零 `drop_until_pts_`

## 3. Player Seek 流程接入

- [x] 3.1 在 `Player::Seek()` 中调用 `video_decoder_.SetDropUntilPts(target_pts)` 和 `audio_decoder_.SetDropUntilPts(target_pts)`
- [x] 3.2 确认 render loop 中 `seek_target_` 逻辑保留不变（兜底）

## 4. 验证

- [x] 4.1 编译通过，无新增 warning
- [x] 4.2 手动测试：seek 2K H.264 视频验证延迟明显降低、画面正确
- [x] 4.3 验证连续快速 seek 不崩溃、不花屏
