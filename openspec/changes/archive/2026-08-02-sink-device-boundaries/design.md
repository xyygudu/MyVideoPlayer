## Context

Sink 节点是管线与外部设备（音频设备、窗口）的交界。交界处的职责划分若含混，症状不是崩溃而是**语义漂移**：时钟报错位置、同一动作两条路径、不变量靠调用方记得配对。本变更把四处这样的边界理清。

## Goals / Non-Goals

**Goals**
- 时钟报告的位置与用户实际感知的位置一致，且不受缓冲深度参数影响。
- 「呈现一帧」与「推进播放位置」是两个可分别观察的动作。
- 一个用户动作（窗口缩放）只有一条路径，且外部设备只被拥有其线程的节点修改。
- 「清空队列」与「递增世代」的配对由实现强制，而非调用方约定。

**Non-Goals**
- 不把 A/V 同步策略抽象为可插拔（需要第二种策略出现才能确定抽象形状）。
- 不改变缓冲深度本身（100ms 保持不变，只是让它不再影响同步）。
- 不动 EffectManager 经裸指针直达节点的现状。
- 不实现视频侧的显示延迟补偿（需要 present 时间戳反馈，SDL3 未直接提供）。

## Decisions

### D1: 音频时钟报告"已呈现位置"，扣除队列深度

```cpp
const double queued = QueuedSeconds();   // 必须在 ConvertAndFeed 之前取
clock_->Set(mf.pts() - queued);
ConvertAndFeed(mf.RawFrame());
```

**为什么在喂入之前取**：喂入前，SDL 队列中的音频恰好覆盖 `[已听到位置, mf.pts())` 这段区间 —— 队列末尾正是即将喂入的这一帧的起点。因此 `已听到位置 = mf.pts() - queued`，是精确等式，不需要估算帧长。若在喂入之后取，队列末尾变成本帧的**终点**，还需减去帧时长，容易出错。

对齐 FFplay 的做法：

```c
set_clock_at(&is->audclk,
             is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size)
                               / is->audio_tgt.bytes_per_sec, ...);
```

FFplay 额外扣了硬件缓冲（`2 * audio_hw_buf_size`）。SDL3 的 `SDL_GetAudioStreamQueued` 只反映 stream 内未被设备取走的字节，不含驱动层缓冲，因此本实现仍会残留一部分（量级 10~30ms）无法消除的超前。这是已知局限，不在本次范围。

实测数据（48kHz 立体声，AAC 1024 samples/frame）：喂入前 0.091~0.100s，喂入后 0.112~0.121s，差值恒为 0.0215s = 1024/48000，锯齿完全规则、无抖动。

### D2: 缓冲深度只保留一个来源

`ShouldThrottle()` 中的 `sample_rate_ * channels_ * 2 / 10` 改为：

```cpp
namespace {
// 在 SDL 中保留这么多音频以吸收喂入抖动。
constexpr double kQueueTargetSeconds = 0.1;
}

bool AudioSinkNode::ShouldThrottle() const {
    if (paused_) return true;
    return QueuedSeconds() > kQueueTargetSeconds;
}
```

`QueuedSeconds()` 同时服务限流与时钟补偿。这样"缓冲多深"只有一个定义处，且改动它不再影响同步 —— 这正是本变更要建立的正交性。

### D3: 呈现与时基推进分离

```cpp
void VideoSinkNode::PresentFrame(MediaFrame frame) {
    current_frame_ = std::move(frame);
    renderer_->Render(current_frame_);
}
```

时钟推进上移到调用点，在**呈现时刻**执行（而非计算延迟之前），保证位置与画面一致：

```cpp
// SyncAndRender 内，sleep 之后
clock_->Set(pts);
PresentFrame(std::move(mf));
```

拆分后 `PresentFrame()` 与 `RedrawCurrent()` 构成对称的一对：前者"换一帧并显示"，后者"重显当前帧"，二者都不隐含时基语义。对齐 FFplay 的 `update_video_pts()` + `video_display()` 两次独立调用。

### D4: 尺寸变化走命令，在渲染线程应用

`kRedraw` 改为 `kResize` 并携带宽高。`Command` 扩展为：

```cpp
enum class CommandType { kSeek, kResize };
struct Command {
    CommandType type;
    double position{0.0};  // kSeek
    int width{0};          // kResize
    int height{0};         // kResize
};
```

字段按类型使用，与现有 `position` 的用法一致。考虑过 `std::variant<SeekArgs, ResizeArgs>`：类型安全但每个消费点都要 `std::get`，在只有两种命令时得不偿失。

`OnCommand` 由 UI 线程调用，**不能**直接碰 `renderer_`（渲染线程独占），只暂存待应用尺寸；渲染循环在自己的线程上应用并重绘：

```cpp
// OnCommand（UI 线程）：宽高打包进单个原子，避免读到不匹配的组合
pending_size_.store((uint64_t(cmd.width) << 32) | uint32_t(cmd.height));

// RenderLoop（渲染线程）
if (uint64_t size = pending_size_.exchange(0)) {
    renderer_->Resize(int(size >> 32), int(size & 0xffffffff));
    RedrawCurrent();
}
```

**这消除了 `VideoRenderer::window_width_/height_` 的数据竞争，且不需要把它们改成原子** —— 因为它们从此只被渲染线程写。竞态在正确的接缝处消失，而不是靠加原子掩盖，这是该接缝选对了的信号。

`MediaPlayer::Impl` 保留 `window_width_/height_` 成员（`VideoRenderer::Open` 需要初始尺寸），但不再调用 `Resize`。图未建立时命令无处可去，此时尺寸仍被记录，供后续 `Open` 使用。

### D5: Flush 自行递增世代，而非私有化

考虑过把 `Flush()` 改为 private。否决 —— 那只是隐藏危险接口，没有消除"清队列必须配世代递增"这条需要人记住的约束。改为让 `Flush()` 自己承担：

```cpp
void MediaGraph::Flush() {
    seek_epoch_.fetch_add(1, std::memory_order_release);   // 必须先于清队列
    for (auto* node : node_ptrs_)
        for (auto* out : node->Outputs()) out->FlushLink();
    for (auto* node : topo_order_) node->Flush();
}

void MediaGraph::Seek(double position) {
    Flush();
    SendCommand({CommandType::kSeek, position});
    for (auto& clock : clocks_) clock->Reset(position);
}
```

递增仍必须先于 `FlushLink()`：flush 会唤醒阻塞在 `Push` 上的生产者，它们随即把手中的旧数据入队，消费者必须已经看到新世代。约束从"调用方要记得先 bump 再 Flush"变成"Flush 内部保证"，且 `Flush()` 成为独立可用的安全操作。

## Risks / Trade-offs

- **同步基准整体后移约 106ms**：视频从钟会相应延后显示。这是修正，但幅度足以被察觉，需人工验收确认没有变成"视频落后"。若显示链路本身有 30~60ms 延迟（DWM 合成 + 显示器），修正后可能出现轻微视频滞后 —— 属于视频侧补偿缺失（范围外），如实测明显再单独处理。
- **驱动层缓冲无法消除**：`SDL_GetAudioStreamQueued` 不含 WASAPI/驱动缓冲，残留 10~30ms 超前。
- **seek 后的首帧**：seek 时 `clock->Reset(position)`，随后第一帧音频到达时队列为空（`FlushSdlBuffer` 已清），补偿为 0，时钟直接设为帧 pts，无跳变。
- **Command 结构膨胀**：新增两个字段，对 kSeek 无意义。两种命令时可接受；若命令类型继续增长到 4 种以上，应改为 variant 承载。
- **`kRedraw` 语义收窄为 `kResize`**：将来若出现"尺寸没变但需要重绘"的场景（如色彩配置变化），需再引入独立的重绘意图。当前唯一触发源就是尺寸变化，不预先泛化。

## Migration Plan

单次变更，无兼容期。`kRedraw` 是上一变更刚引入的内部枚举值，仅两处使用。

## Open Questions

无。
