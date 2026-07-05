## Context

`MediaGraph::Seek(pos)` 的执行分两步：`Flush()`（清空 Link、递增 serial）+ `SendCommand({kSeek, pos})`（拓扑序广播）。各节点响应：

- **DemuxNode**：`OnCommand` 在主线程只设 `seek_position_`/`seek_requested_` 两个原子；真正的 `av_seek_frame` 由 demux 线程在 `HandlePendingSeek()` 异步执行，处理完 `seek_requested_.store(false)`。
- **DecoderNode**：`OnCommand` 在主线程同步调用 `SetDropUntilPts(pos)`，立即更新 `drop_until_pts_`，并（错误地）写 `codec_ctx_->skip_frame`。

日志证据（连续 seek 冻结）：

| 组件 | 最终目标 | 说明 |
|------|---------|------|
| decoder `drop_until_pts_` | 472.752 | 每次 seek 同步更新，反映最新 |
| demux `av_seek_frame` | 363.607 | 吞掉了 472.752 请求，停在上一次 |

## Goals

- demux 处理的 seek 目标与 decoder 的 drop target 在任意 seek 序列下保持一致
- 快速连续 seek 不丢失最新 seek 请求
- seek flush 后不把 pre-seek 陈旧包混入 post-seek 流
- 消除主线程对解码线程 `codec_ctx_` 的写访问

## Decisions

### 1. demux seek 请求：不可丢失语义（修 lost-update）

**问题**：`HandlePendingSeek` 读 `seek_position_` → seek → `seek_requested_.store(false)`，若期间新 seek 设了 `requested=true`，会被这次 `store(false)` 覆盖丢失。

**方案**：合并为单个 `std::atomic<double> pending_seek_`，用哨兵值 `-1`（`kNoSeekPending`）表示"无待处理请求"（seek 位置恒为非负，哨兵安全）。
- `RequestSeek(pos)`：`pending_seek_.store(pos)`，直接覆盖，只关心最新目标。
- `HandlePendingSeek`：`pending_seek_.exchange(kNoSeekPending)` 原子地"读出并清空"，避免了两个独立原子之间的更新窗口，无锁、单变量、无 ABA 风险。

（曾考虑 mutex + `std::optional<double>` 方案，语义等价但需要额外的锁；由于 seek 位置永不为负，哨兵值方案更简洁，两者正确性一致。）

### 2. 陈旧在途包隔离（对齐 FFplay serial-at-read）

**问题**：`Link::Push` 在**入队时**才盖 serial。阻塞在 Push 中的 pre-seek 包，被 `Flush()` 唤醒后盖上新 serial 入队，被 decoder 当作 post-seek 有效包，flush 后立即解码 GOP 中间帧 → h264 报错。

**方案**：serial 绑定时机前移。Demux 读出包时即记录当时 serial（来源于 Link 当前 serial 或 demux 本地 serial），随包携带；`Link::Flush` 只递增队列 serial。消费端（decoder）在 Pop/处理时比对包携带的 serial 与 Link 当前 serial，不一致即丢弃。这样被唤醒的陈旧包因 serial 落后而被丢弃，不再污染解码器。

与既有 `demux-decode` spec 的 "Demux updates serial after seek" / "Demuxer 维护本地 serial 副本" 方向一致，本次将该语义落到 graph 版 `Link`。

### 3. 去除主线程写 codec_ctx_

`SetDropUntilPts` 删除对 `codec_ctx_->skip_frame` 的写，只保留 `drop_until_pts_.store()`。skip_frame 的设置已由解码线程的 `MaybeFlushOnSerialChange`（serial 变化时）负责，线程安全。

### 4. 移除临时诊断

删除 `[SEEK-DIAG]` 日志（decoder_node、demux_node）与 `main.cc` 的 `EnableFileLogging("logs/app.log")` 及相关 `diag_after_seek_` 成员。

## Risks / Trade-offs

- **[Risk] serial 前移改动触及 Link/Demux/Decoder 的 serial 协议** → 分步实施，先修 demux lost-update（直接消除本次冻结），再做 serial 前移（消除 h264 报错）；两者独立可验证
- **[Risk] 哨兵值 -1 依赖"seek 位置永不为负"的隐含假设** → 调用方（MediaPlayer::Seek）已将位置 clamp 到 [0, duration]，风险可控
- **[Risk] 去掉主线程 skip_frame 写后首帧可能少一次 NONREF 加速** → skip_frame 仍由解码线程在 flush 时设置，seek 加速效果不变
