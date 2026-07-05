## 1. 修复 demux seek 请求 lost-update（主修复：消除冻结）

- [x] 1.1 将 DemuxNode 的 `seek_position_` + `seek_requested_` 两个原子改为单个 `std::atomic<double> pending_seek_`，哨兵值 `-1` 表示无请求
- [x] 1.2 `RequestSeek(pos)` 直接 `pending_seek_.store(pos)`（新请求永不丢失）
- [x] 1.3 `HandlePendingSeek` 用 `pending_seek_.exchange(-1)` 原子地取出并清空
- [x] 1.4 手动验证：连续快速 seek 多次，demux 最终 seek 位置与 decoder drop target 一致，不再冻结

## 2. 陈旧在途包隔离（消除 h264 报错）

- [x] 2.1 让包在读出时携带 serial：demux 读出包后按当前 serial 打标（而非 Link 入队时打标）
- [x] 2.2 `Link::Push` 不再覆盖包的 serial；`Link::Flush` 仅递增队列 serial
- [x] 2.3 消费端在 Pop/处理时比对包 serial 与 Link 当前 serial，落后则丢弃
- [x] 2.4 手动验证：seek 时不再打印 `Missing reference picture` / `mmco: unref short failure`

## 3. 去除跨线程 codec 访问（线程安全）

- [x] 3.1 `DecoderNode::SetDropUntilPts` 删除对 `codec_ctx_->skip_frame` 的写，只保留 `drop_until_pts_.store()`
- [x] 3.2 确认 `MaybeFlushOnSerialChange` 仍在解码线程正确设置 skip_frame

## 4. 移除临时诊断日志

- [x] 4.1 删除 `decoder_node.cc` 中所有 `[SEEK-DIAG]` 日志（SetDropUntilPts / DROP / RESUME / flush_buffers / PULL）
- [x] 4.2 删除 `demux_node.{h,cc}` 中 `[SEEK-DIAG]` 日志与 `diag_after_seek_` 成员
- [x] 4.3 删除 `main.cc` 的 `EnableFileLogging("logs/app.log")` 调用及注释

## 5. 编译与回归验证

- [x] 5.1 `cmake --build build` 编译通过
- [x] 5.2 回归：正常单次 seek、连续快速 seek、seek 到近末尾均不冻结、无 h264 报错
