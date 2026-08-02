## 1. 时钟接口与线程安全

- [x] 1.1 `IClock` 从 `graph/media_graph.h` 移入 `clock.h`（`mvp::graph` → `mvp`），补 `virtual void Reset(double pts) = 0`
- [x] 1.2 `Clock : public IClock`，四个写方法加 `override`
- [x] 1.3 `Clock` 新增 `std::mutex write_mutex_`，`Set/SetPaused/SetSpeed/Reset` 全程持锁，读端 `Get()` 保持无锁

## 2. 图级主时钟仲裁

- [x] 2.1 `node.h` 新增 `struct ClockOffer { std::shared_ptr<IClock> clock; int priority{0}; }` 与 `virtual ClockOffer ProvideClock() { return {}; }`
- [x] 2.2 `MediaGraph` 移除 `SetClock/Clock()`，新增 `IClock* MasterClock() const`，成员改为 `clocks_` + `master_clock_`
- [x] 2.3 `MediaGraph::SelectMasterClock()`：收集全部 offer，取优先级最高者（平手按拓扑序），在 `Negotiate()` 中于 `ValidateCaps` 之后、节点 `Negotiate()` 之前调用
- [x] 2.4 `MediaGraph::SetPaused()` 在暂停节点后广播 `SetPaused` 至所有时钟
- [x] 2.5 `MediaGraph::Seek()` 在广播 seek 命令后广播 `Reset(position)` 至所有时钟

## 3. 节点迁移

- [x] 3.1 `AudioSinkNode`：自持 `shared_ptr<Clock>`，实现 `ProvideClock()`；删除 `SetAudioClock` 与裸指针成员；`AudioLoop` 无条件更新自身时钟
- [x] 3.2 `VideoSinkNode`：自持 `shared_ptr<Clock>`，实现 `ProvideClock()`（优先级低于音频）；`Negotiate()` 读取 `graph_->MasterClock()`
- [x] 3.3 `VideoSinkNode`：删除 `SyncMode` 枚举、`SetSyncMode`、`SetAudioClock`、`SetVideoClock` 及对应成员
- [x] 3.4 `ComputeDisplayDelay` 改判"主时钟是否为自身"；`ComputeAudioMasterDelay` → `ComputeSlavedDelay`，`ComputeVideoMasterDelay` → `ComputeFreeRunDelay`

## 4. Facade 清理

- [x] 4.1 `MediaPlayer::Impl` 删除 `audio_clock_` / `video_clock_` 两个成员
- [x] 4.2 `Play()`/`Pause()`/`Seek()`/`Close()` 删除直接操作时钟的语句，全部由 `graph_->SetPaused`/`graph_->Seek` 承担
- [x] 4.3 `CurrentPosition()` 改为查询 `graph_->MasterClock()`，null 时返回 0
- [x] 4.4 `OnGraphEvent(kEos)` 改为 `graph_->SetPaused(true)`
- [x] 4.5 `BuildGraph()` 删除 3 处时钟 setter 与 `SetSyncMode`

## 5. 验证

- [x] 5.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 5.2 `mvp_transcode_cli` 回归：转码图无时钟提供者，`MasterClock()` 为 null，mkv/mpegts 均完整转码
- [x] 5.3 启动 `mvp_app` 无报错；音视频同步、seek、pause、播放结束重播由人工验收
