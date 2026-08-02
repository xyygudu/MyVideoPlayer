## 1. MediaFrame 退回纯数据

- [ ] 1.1 删除 `pts_` / `type_` 成员与 `pts()` / `type()` 访问器
- [ ] 1.2 构造函数收窄为 `explicit MediaFrame(AVFrame* src)`；移动构造/赋值相应简化
- [ ] 1.3 `MakeWritable()` 两个重载去掉 pts/type 拷贝
- [ ] 1.4 删除死代码 `CreateSameFormat`（零调用点，已被 MediaFramePool 取代）
- [ ] 1.5 `MediaFramePool::Acquire(w, h, fmt)` 去掉 pts 参数，不再设置 type
- [ ] 1.6 头注释修掉已不存在的 `FrameQueue`，并写明 `AVFrame::pts` 仅在进出 FFmpeg 的边界有效

## 2. MediaBuffer 元数据收窄

- [ ] 2.1 删除 `media_type_` 成员与 `media_type()` 访问器
- [ ] 2.2 两个构造函数统一为 `(payload, Timestamp, BufferFlags)`，移除 MediaType 参数
- [ ] 2.3 `MakeEos(MediaType, int serial)` → `MakeEos(int serial)`
- [ ] 2.4 `Timestamp` 删除 `dts` 与 `duration`；为 `time_base` 加注释说明仅对包载荷有意义

## 3. 调用点迁移

- [ ] 3.1 `DemuxNode`：`RoutePacket` / `EmitEos` 去掉 MediaType 参数，不再写 `ts.dts` / `ts.duration`
- [ ] 3.2 `DecoderNode`：`MediaFrame mf(frame.get())`，`MediaBuffer buf(std::move(mf), ts)`，不再写 `ts.duration`
- [ ] 3.3 `TransformEffectNode`：`Acquire(w, h, fmt)` 去掉 pts
- [ ] 3.4 `ColorEffectNode`：构造点适配
- [ ] 3.5 `EncoderNode`：`Acquire` 去掉 pts；`pts_ticks` 改由 `buf.timestamp().pts` 计算；输出不再写 `ts.dts` / `ts.duration`
- [ ] 3.6 `VideoSinkNode`：`SyncAndRender` 改收 `MediaBuffer`；暂停分支从 `buf.timestamp().pts` 取 pts
- [ ] 3.7 `AudioSinkNode`：`clock_->Set(buf.timestamp().pts - QueuedSeconds())`

## 4. 验证

- [ ] 4.1 `cmake --build build` 通过，`get_errors` 无告警
- [ ] 4.2 `mvp_transcode_cli` 回归：mkv/mpegts 输出与改动前**逐字节一致**（MuxNode 仍依赖 `ts.pts` 与 `ts.time_base`，两者未动）
- [ ] 4.3 全项目搜索确认 `mf.pts()` / `.type()` / `media_type()` / `ts.dts` / `ts.duration` 无残留
- [ ] 4.4 人工验收：播放正常、音画同步、seek 与暂停行为不变（纯类型重构，预期零变化）
