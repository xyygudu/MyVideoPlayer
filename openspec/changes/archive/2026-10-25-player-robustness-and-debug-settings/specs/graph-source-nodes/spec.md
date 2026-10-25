## ADDED Requirements

### Requirement: DemuxNode 时间轴从 0 开始
DemuxNode SHALL 将每个输出包的 pts/dts 减去容器 `start_time`，使时间轴从 0 开始（MPEG-TS 常从 1.4 秒或任意偏移开始）；seek 目标 SHALL 按同一偏移换算回容器时间。

#### Scenario: 起点为 10 秒的 TS
- **WHEN** 播放时间戳从 10 秒开始的 TS 文件
- **THEN** 起播时 `CurrentPosition()` 与屏幕帧 pts 都小于 2 秒，`Duration()` 约为文件实际时长

### Requirement: DemuxNode 可选折叠时间戳跳变
DemuxNode SHALL 接受 `DemuxOptions{correct_ts_discontinuities}`（默认 false）。开启且容器带 `AVFMT_TS_DISCONT` 标志时，DemuxNode SHALL 按流跟踪期望的下一个 DTS，当实际 DTS 偏离超过阈值或明显后退时，把偏差累加进一个跨流共享的偏移量并平移此后所有包（ffmpeg 命令行 `ts_discontinuity_detect` 语义），记录告警；seek 时 SHALL 清空跟踪状态与偏移。播放 SHALL 不开启（ffplay 语义）。

#### Scenario: 折叠后跳
- **WHEN** 开启折叠，某流 DTS 从 4.4 秒跳回 1.4 秒
- **THEN** 后续包整体平移 3 秒，输出时间轴连续，日志记录一次跳变
