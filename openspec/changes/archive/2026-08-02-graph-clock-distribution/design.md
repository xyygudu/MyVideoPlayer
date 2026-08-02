## Context

播放图需要一个全局时基供各 sink 对齐。当前实现把"选主"的决策散落在 facade 与节点里，时钟对象由 facade 持有、由节点写入，两者之间靠手工 setter 穿线。本设计把选主收敛为图级仲裁，把时钟所有权还给写入方。

## Goals / Non-Goals

**Goals**
- 主时钟的选择只存在一处，且不依赖节点语义硬编码。
- 时钟由写入它的节点拥有；消费方通过图查询，不经 facade 转手。
- pause/seek 对时钟的影响只有一条广播路径。
- 修复 `Clock` 的多写者数据竞争。

**Non-Goals**
- 不实现外部时钟（external clock）节点，只保证接入时零改动现有节点。
- 不实现运行时切换主时钟（`SelectMasterClock` 只在协商期执行一次）。
- 不改动 `frame_timer_` 累积算法本身，只改它的参考基准来源。
- 不实现变速播放（`SetSpeed` 仍无调用点）。

## Decisions

### D1: 主时钟仲裁模型对齐 VLC，而非 FFplay

| | 时钟对象 | 谁选主 | 运行时切换 |
|---|---|---|---|
| FFplay | `VideoState` 内嵌 `audclk`/`vidclk`/`extclk` | `av_sync_type` 枚举 + `get_master_clock()` 三分支 switch | 仅命令行 `-sync` |
| MPV | 无独立时钟对象，位置从 AO 查询 | AO 存在即音频主导；视频侧靠 `--video-sync` 策略族 | 配置项 |
| VLC | `vlc_clock_main_t` + 每 ES 一个 `vlc_clock_t` | `es_out` 仲裁，显式 `SetMaster`/`SetSlave` | 可运行时换 master |

FFplay 的 switch 正是当前实现的原型，也是问题源头：新增时钟类型 = 新增一个 case + 所有调用点跟改。VLC 的模型与本项目 graph 同构（`es_out ≈ MediaGraph` 仲裁者，`ES ≈ Node`），节点不知道自己是不是主，只负责"报出时基"和"问该等多久"。采用 VLC 模型。

### D2: `ClockOffer{clock, priority}` 而非"先到先得"

`ProvideClock()` 返回带优先级的 offer，图取优先级最高者（平手按拓扑序）。

- 若用"拓扑序第一个提供者胜"，graph 就隐含依赖建图顺序——`MediaPlayer::BuildGraph` 调整节点添加次序会静默改变主时钟，是隐蔽的耦合。
- 若让 graph 按 `NodeType`/节点名判断"音频优先"，graph 就侵入了节点语义。
- 优先级由节点自己声明，graph 保持语义无关；未来外部时钟节点报更高优先级即可夺主，无需改 graph。

优先级常量定义在各节点实现文件内（`kClockPriority`），不进 `node.h`，避免公共头承载"音频/视频谁更重要"的语义。

### D3: `SyncMode` 由"我是不是主钟"取代

```cpp
if (master_clock_ && master_clock_ != clock_.get()) return ComputeSlavedDelay(...);
return ComputeFreeRunDelay(...);
```

原 `kAudioMaster`/`kVideoMaster` 表达的真实语义是"有无外部参考时基"：
- 主钟是别人 → 我是从钟，按 `frame_timer_` 累积算法向主钟收敛（原 `ComputeAudioMasterDelay`）。
- 主钟是我自己 → 无外部参考，按帧间隔自由走时（原 `ComputeVideoMasterDelay`）。

据此重命名两个函数为 `ComputeSlavedDelay` / `ComputeFreeRunDelay`。接入外部时钟后，VideoSinkNode 自动降为从钟，代码一行不改。

### D4: 时钟所有权归写入方，图持共享引用

节点持有 `std::shared_ptr<mvp::Clock>`，图收集为 `std::vector<std::shared_ptr<IClock>> clocks_` 并选出 `std::shared_ptr<IClock> master_clock_`。

- `shared_ptr` 保证图在节点析构后仍可安全读取（`MediaGraph` 析构时节点先于成员销毁的场景）。
- `MasterClock()` 返回裸 `IClock*`（非拥有观察者），避免 UI 每帧查询位置时的原子引用计数开销。
- 图保留 `clocks_` 全集而非仅 master：pause/seek 需要广播到所有时钟（保持现有行为——两个时钟同时冻结/重置）。

### D5: 仲裁时机放在 `Negotiate()` 内的第三步

```
1. reverse-topo  DeclareCaps()      节点声明需求
2.               ValidateCaps()     图级仲裁/校验
3.               SelectMasterClock() 图级仲裁          ← 新增
4. topo-order    Negotiate()        节点消费仲裁结果
```

与既有的 caps 两趟协商完全同构。放在 `Open()` 末尾亦可行，但会把"获取资源"与"图级仲裁"混进同一方法；放在 `Negotiate()` 内既不新增生命周期阶段，语义也更贴切——选主本质是一次图范围的协商。

### D6: `Clock` 补齐写者互斥，成为完整 seqlock

现实现只有 `seqcount_t`（单写者不变量），但 UI 线程的 `SetPaused`/`Reset` 与音频线程的 `Set` 并发写入，违反不变量。

考虑过的替代方案：把时钟写入通过 `SendCommand` 派发到节点线程执行。否决——`AudioLoop`/`RenderLoop` 在非暂停且数据饥饿时阻塞在 `input_port_->Pull()`，暂停指令会滞留到下一帧到达，期间时钟仍按墙钟外推，进度条继续走。用线程跳转换正确性反而引入延迟缺陷。

采用 Linux `seqlock_t = seqcount_t + 写者锁` 的标准做法：写者之间用 `std::mutex` 串行化，**读端保持完全无锁**（SeqLock 的全部价值）。代价为每次写入一次无竞争 mutex，在音频帧率（约 40 次/秒）下可忽略。

### D7: `IClock` 下沉到 `clock.h`

`Clock` 要实现 `IClock`，而 `IClock` 现居 `graph/media_graph.h`——若 `clock.h` 反向 include 会把整个 `MediaGraph` 声明拖进这个叶子头文件。改为把 `IClock` 移入 `clock.h`（命名空间 `mvp::graph` → `mvp`），`media_graph.h` include `clock.h`。`clock.h` 只依赖 `<atomic>/<cstdint>/<mutex>`，是无依赖叶子，被下层 include 不构成循环或反向依赖。

`IClock` 补 `virtual void Reset(double)`（graph seek 广播需要）。`MediaGraph::Clock()` 更名 `MasterClock()`，同时避开与 `mvp::Clock` 的名字遮蔽。

## Risks / Trade-offs

- **A/V 同步核心路径**：`ComputeDisplayDelay` 的分支判据整体替换。算法本身不变，但选支条件变了。→ CLI 无法覆盖，需人工验收多音轨/无音轨/seek/pause 场景。
- **EOS 行为变化**：原先 EOS 只冻结时钟；现改为 `graph_->SetPaused(true)`，节点也一并暂停。EOS 时所有 sink 已报完 EOS、工作已结束，理论无副作用；`Finished → Seek(0) → Play()` 路径会经 `SetPaused(false)` 恢复。→ 需验证播放结束后重播正常。
- **seek 时的写入竞争**：`graph_->Seek()` 先 `Flush()` 丢弃在途 buffer 再 `Reset()` 时钟，但音频线程仍可能在极小窗口内用 seek 前的 pts 覆盖。此为既有行为，seqlock 写者锁只保证不撕裂、不保证语义顺序。→ 现状保留，如实测有跳动再单独处理。
- **主时钟为空**：转码图无任何节点提供时钟，`MasterClock()` 返回 nullptr。VideoSinkNode 与 `CurrentPosition()` 均有 null 保护。

## Migration Plan

单次变更，无兼容期。删除的都是内部 setter，公共 API 不受影响。

## Open Questions

无。
